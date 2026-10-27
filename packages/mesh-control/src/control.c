/* The control port: the client loop, the request dispatch, and the texts it answers with. */
#include "mesh/control.h"
#include "mesh/abi.h"
#include "mesh/backend.h"
#include "mesh/infer_request.h"
#include "mesh/inferd.h"
#include "mesh/load_text.h"
#include "mesh/loader.h"
#include "mesh/log.h"
#include "mesh/prog_table.h"
#include "mesh/rsfmt.h"
#include "mesh/snapshot.h"

#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

bool (*mesh_control_render)(mesh_shared *sh, char **words, size_t n, uint8_t **out, size_t *len, char **err);

#define LIFECYCLE_MS 30000
#define TASK_MS 10000
#define PLACE_MS 15000
#define FORWARD_WAIT_MS 70000 /* peer.rs FORWARD_WAIT */
#define REQ_MAX ((size_t)1 << 20)
#define LINE_MAX 4096
#define MAX_CLIENTS 32
#define CLIENT_IDLE_MS 120000
#define CLIENT_WRITE_MS 10000

/* ---------------------------------------------------------------- reply shapes */

static void framed(mesh_buf *out, const void *b, size_t n) {
    mesh_buf_u32le(out, (uint32_t)n);
    mesh_buf_put(out, b, n);
}

static void framed_str(mesh_buf *out, const char *s) { framed(out, s, strlen(s)); }

static void framed_err(mesh_buf *out, const char *e) {
    mesh_buf t = MESH_BUF_INIT;
    mesh_buf_printf(&t, "err %s\n", e);
    framed(out, t.data, t.len);
    mesh_buf_free(&t);
}

static void line(mesh_buf *out, const char *s) { mesh_buf_printf(out, "%s\n", s); }

/* Ok("") is `ok`, Ok(s) `ok s`, Err(e) `err e`. */
static void reply(mesh_buf *out, bool ok, const char *text) {
    if (ok && !*text) line(out, "ok");
    else mesh_buf_printf(out, "%s %s\n", ok ? "ok" : "err", text);
}

/* ---------------------------------------------------------------- small helpers */

static uint32_t rd32(const uint8_t *b, size_t o) { return (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24; }
static uint64_t rd64(const uint8_t *b, size_t o) { return (uint64_t)rd32(b, o) | (uint64_t)rd32(b, o + 4) << 32; }
static float rdf(const uint8_t *b, size_t o) { uint32_t u = rd32(b, o); float f; memcpy(&f, &u, 4); return f; }

/* A NUL-padded string field as text. */
static void cstr(mesh_buf *out, const uint8_t *b, size_t n) {
    size_t k = 0;
    while (k < n && b[k]) k++;
    mesh_rs_lossy(out, b, k);
}

/* `0x` hex as Rust's {:#x}. */
static void hexalt(mesh_buf *b, uint64_t v) { mesh_buf_printf(b, "0x%llx", (unsigned long long)v); }

/* "0x.." or decimal u32 (control.rs int). */
static bool int_(const char *s, uint32_t *v) {
    if (strncmp(s, "0x", 2) == 0) {
        const char *h = s + 2;
        size_t i = (*h == '+' && h[1]) ? 1 : 0;
        if (!h[i]) return false;
        uint64_t x = 0;
        for (; h[i]; i++) {
            char c = h[i];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) return false;
            x = x * 16 + (uint64_t)d;
            if (x > UINT32_MAX) return false;
        }
        *v = (uint32_t)x;
        return true;
    }
    return mesh_rs_parse_u32(s, strlen(s), v) == 0;
}

static bool is_ws(char c) { return c == ' ' || (c >= 9 && c <= 13); }

/* split_whitespace: up to max words (pointers into a copy the caller frees). */
static size_t split_ws(char *s, char **w, size_t max) {
    size_t n = 0;
    while (*s) {
        while (*s && is_ws(*s)) s++;
        if (!*s) break;
        if (n < max) w[n] = s;
        n++;
        while (*s && !is_ws(*s)) s++;
        if (*s) *s++ = 0;
    }
    return n;
}

static char *debug_q(const char *s) {
    mesh_buf b = MESH_BUF_INIT;
    mesh_rs_debug_cstr(&b, s);
    return mesh_buf_take(&b);
}

/* In a fleet, and name is the loaded lockstep program (its input and requests are refused). */
static bool lockstep(mesh_shared *sh, const char *name) {
    uint32_t s;
    return sh->peer && mesh_pt_find(sh->mb, name, &s) && mesh_mailbox_get(sh->mb, mesh_prog_entry(s) + MESH_PT_PEER) == MESH_PEER_MODE_LOCKSTEP;
}

/* A change of the desired state (fleet), answered by the leader. */
static void change(mesh_shared *sh, const char *req, mesh_buf *out) {
    char *text = NULL;
    bool ok = mesh_shared_change(sh, req, FORWARD_WAIT_MS + 5000, &text);
    if (!ok) {
        char *q = debug_q(req);
        mesh_error("control: %s: %s", q, text);
        free(q);
    }
    reply(out, ok, text);
    free(text);
}

static void ask(mesh_shared *sh, mesh_cmd c, int64_t within, mesh_buf *out) {
    char *text = NULL;
    bool ok = mesh_shared_ask(sh, c, within, &text);
    reply(out, ok, text);
    free(text);
}

/* ---------------------------------------------------------------- texts */

static void names_of(mesh_shared *sh, uint32_t mask, mesh_buf *out, const char *none) {
    size_t at = out->len;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (!((mask >> s) & 1u)) continue;
        char n[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(sh->mb, s, n);
        mesh_buf_printf(out, "%s%s", out->len > at ? "," : "", n);
    }
    if (out->len == at) mesh_buf_puts(out, none);
}

/* An inbox's (pushed, popped, refused, empty pops, waiting): its two sides' ring counters summed. */
static void inbox_counts(mesh_shared *sh, mesh_pipe_inbox in, uint32_t c[5]) {
    mesh_words w = mesh_mailbox_words(sh->mb);
    mesh_pipe_ring a = mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_CPU), b = mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_GPU);
    const size_t offs[4] = {MESH_PIPE_PI_HEAD, MESH_PIPE_PI_TAIL, MESH_PIPE_PI_FULL, MESH_PIPE_PI_EMPTY};
    for (int i = 0; i < 4; i++) c[i] = mesh_words_load(&w, a.base + offs[i]) + mesh_words_load(&w, b.base + offs[i]);
    c[4] = mesh_pipe_inbox_len(&in, &w);
}

static bool inbox_of(mesh_shared *sh, uint32_t s, mesh_pipe_inbox *in, char *ty) {
    mesh_mailbox mb = sh->mb;
    if (mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) return false;
    size_t p = mesh_pipe_entry(s);
    uint32_t cap = mesh_mailbox_get(mb, p + MESH_PP_IN_CAP), rec = mesh_mailbox_get(mb, p + MESH_PP_IN_REC);
    if (!cap) return false;
    in->base = (size_t)mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_VIEW_OFF) + mesh_mailbox_get(mb, p + MESH_PP_IN_OFF);
    in->cap = cap;
    in->rec = rec;
    uint8_t t[MESH_NAME_BYTES];
    mesh_mailbox_read(mb, p + MESH_PP_TYPE_NAME, t, MESH_NAME_BYTES);
    mesh_buf b = MESH_BUF_INIT;
    mesh_rs_lossy(&b, t, MESH_NAME_BYTES);
    /* trim_end_matches('\0') */
    while (b.len && b.data[b.len - 1] == 0) b.len--;
    snprintf(ty, 64, "%.*s", (int)b.len, b.data ? b.data : "");
    mesh_buf_free(&b);
    return true;
}

void mesh_control_pipes(mesh_shared *sh, mesh_buf *out) {
    mesh_mailbox mb = sh->mb;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        mesh_pipe_inbox in;
        char ty[64];
        if (!inbox_of(sh, s, &in, ty)) continue;
        uint32_t c[5];
        inbox_counts(sh, in, c);
        char n[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, n);
        mesh_buf_printf(out, "inbox %s type %s cap %u rec %u head %u tail %u len %u full %u empty %u fed_by ", n, ty, in.cap, in.rec, c[0], c[1], c[4],
                        c[2], c[3]);
        names_of(sh, mesh_mailbox_get(mb, mesh_pipe_entry(s) + MESH_PP_FED_MASK), out, "-");
        mesh_buf_putc(out, '\n');
    }
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        uint32_t f = mesh_mailbox_get(mb, mesh_pipe_entry(s) + MESH_PP_FEEDS_MASK);
        if (!f || mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
        char n[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, n);
        mesh_buf_printf(out, "feeds %s -> ", n);
        names_of(sh, f, out, "-");
        mesh_buf_putc(out, '\n');
    }
}

void mesh_control_pipes_recs(mesh_shared *sh, mesh_recs *v) {
    mesh_mailbox mb = sh->mb;
    int64_t now = mesh_mono_ns();
    mesh_recs inboxes = MESH_RECS_INIT;
    char seen[MESH_PROG_SLOTS][MESH_NAME_BYTES + 1];
    size_t nseen = 0;
    pthread_mutex_lock(&sh->mu);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        mesh_pipe_inbox in;
        char ty[64];
        if (!inbox_of(sh, s, &in, ty)) continue;
        uint32_t c[5];
        inbox_counts(sh, in, c);
        uint32_t head = c[0], tail = c[1], full = c[2], empty = c[3], depth = c[4];
        char name[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, name);
        /* Rates since the last sample (>= 0.5 s ago); a reload (counts going back) restarts them. */
        mesh_pipe_sample *e = NULL;
        for (size_t i = 0; i < sh->npipe_hist; i++) if (strcmp(sh->pipe_hist[i].name, name) == 0) e = &sh->pipe_hist[i];
        if (!e) {
            sh->pipe_hist = mesh_xrealloc(sh->pipe_hist, (sh->npipe_hist + 1) * sizeof *sh->pipe_hist);
            e = &sh->pipe_hist[sh->npipe_hist++];
            memset(e, 0, sizeof *e);
            snprintf(e->name, sizeof e->name, "%s", name);
            e->at_ns = now;
            e->head = head;
            e->tail = tail;
            e->full = full;
        }
        float dt = (float)((double)(now - e->at_ns) / 1e9);
        if (head < e->head || tail < e->tail || full < e->full) {
            e->at_ns = now;
            e->head = head;
            e->tail = tail;
            e->full = full;
            e->rates[0] = e->rates[1] = e->rates[2] = 0;
        } else if (dt >= 0.5f) {
            e->rates[0] = (float)(head - e->head) / dt;
            e->rates[1] = (float)(tail - e->tail) / dt;
            e->rates[2] = (float)(full - e->full) / dt;
            e->at_ns = now;
            e->head = head;
            e->tail = tail;
            e->full = full;
        }
        float rates[3] = {e->rates[0], e->rates[1], e->rates[2]};
        snprintf(seen[nseen++], MESH_NAME_BYTES + 1, "%s", name);
        mesh_rec *r = mesh_recs_add(&inboxes, "inbox");
        mesh_rec_kv(r, "consumer", name);
        mesh_rec_kv(r, "type", ty);
        mesh_rec_kvu(r, "cap", in.cap);
        mesh_rec_kvu(r, "rec", in.rec);
        mesh_rec_kvu(r, "depth", depth);
        mesh_rec_f(r, "fill", true, (double)depth / (double)(in.cap > 1 ? in.cap : 1), 3);
        mesh_rec_kvu(r, "pushed", head);
        mesh_rec_kvu(r, "popped", tail);
        mesh_rec_kvu(r, "refused", full);
        mesh_rec_kvu(r, "empty", empty);
        mesh_buf fb = MESH_BUF_INIT;
        names_of(sh, mesh_mailbox_get(mb, mesh_pipe_entry(s) + MESH_PP_FED_MASK), &fb, "");
        mesh_rec_kv(r, "fed_by", mesh_buf_str(&fb));
        mesh_buf_free(&fb);
        mesh_rec_f(r, "in_s", true, rates[0], 1);
        mesh_rec_f(r, "out_s", true, rates[1], 1);
        mesh_rec_f(r, "refused_s", true, rates[2], 1);
    }
    size_t k = 0;
    for (size_t i = 0; i < sh->npipe_hist; i++) {
        bool keep = false;
        for (size_t j = 0; j < nseen; j++) keep |= strcmp(seen[j], sh->pipe_hist[i].name) == 0;
        if (keep) sh->pipe_hist[k++] = sh->pipe_hist[i];
    }
    sh->npipe_hist = k;
    pthread_mutex_unlock(&sh->mu);
    mesh_rec *p = mesh_recs_add(v, "pipes");
    mesh_rec_kvu(p, "system", mesh_host_block_get_u32(&sh->host, MESH_S_SYSTEM));
    mesh_rec_kvu(p, "inboxes", inboxes.len);
    mesh_recs_extend(v, &inboxes);
    mesh_recs_free(&inboxes);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        uint32_t f = mesh_mailbox_get(mb, mesh_pipe_entry(s) + MESH_PP_FEEDS_MASK);
        if (!f || mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
        char n[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, n);
        mesh_rec *r = mesh_recs_add(v, "feed");
        mesh_rec_kv(r, "producer", n);
        mesh_buf cb = MESH_BUF_INIT;
        names_of(sh, f, &cb, "");
        mesh_rec_kv(r, "consumers", mesh_buf_str(&cb));
        mesh_buf_free(&cb);
    }
}

int mesh_control_view(mesh_shared *sh, const char *which, bool json, mesh_buf *out, char **err) {
    mesh_recs recs = MESH_RECS_INIT;
    if (strcmp(which, "fleet") == 0 && sh->peer) {
        pthread_mutex_lock(&sh->mu);
        mesh_recs_extend(&recs, &sh->fleet_view);
        pthread_mutex_unlock(&sh->mu);
    } else if (strcmp(which, "fleet") == 0) {
        mesh_rec *r = mesh_recs_add(&recs, "fleet");
        mesh_rec_kvu(r, "system", mesh_host_block_get_u32(&sh->host, MESH_S_SYSTEM));
        mesh_rec_kvu(r, "n", 1);
        mesh_rec_kv(r, "role", "single");
        mesh_rec_text(r, "not in a fleet (single meshd: --cluster / --peer)");
    } else if (strcmp(which, "jobs") == 0) {
        pthread_mutex_lock(&sh->mu);
        mesh_recs_extend(&recs, &sh->jobs_view_recs);
        pthread_mutex_unlock(&sh->mu);
    } else if (strcmp(which, "pipes") == 0) {
        mesh_control_pipes_recs(sh, &recs);
    } else {
        mesh_buf b = MESH_BUF_INIT;
        mesh_buf_puts(&b, "view ");
        mesh_rs_debug_cstr(&b, which);
        mesh_buf_puts(&b, ": fleet, jobs or pipes [json]");
        *err = mesh_buf_take(&b);
        return -1;
    }
    mesh_views_render(out, &recs, json);
    mesh_recs_free(&recs);
    return 0;
}

void mesh_control_programs(mesh_shared *sh, mesh_buf *out) {
    mesh_mailbox mb = sh->mb;
    uint8_t *host = malloc(MESH_SNAP_HOST);
    mesh_host_block_bytes(&sh->host, host);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (mesh_mailbox_get(mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
        uint8_t e[MESH_PT_BYTES];
        mesh_pt_read_entry(mb, s, e);
        char name[MESH_NAME_BYTES + 1];
        mesh_pt_name_of(mb, s, name);
        float rate = rdf(host, MESH_S_RATES + 4 * (size_t)s);
        uint64_t hash = rd64(e, MESH_PT_PTX_HASH);
        uint32_t st = rd32(e, MESH_PT_STATE), pol = rd32(e, MESH_PT_POLICY);
        size_t c = mesh_prog_ctl(s), p = mesh_pload(s);
        float cpu = rdf(host, p + MESH_PL_CPU);
        double rss = (double)rd64(host, p + MESH_PL_RSS) / 1048576.0;
        uint32_t units = rd32(host, p + MESH_PL_UNITS);
        mesh_buf kind = MESH_BUF_INIT;
        uint32_t ex = mesh_mailbox_get(mb, mesh_prog_ext(s) + MESH_PX_EXEC);
        if (ex == MESH_BACKEND_NATIVE) {
            uint32_t exit_ = mesh_mailbox_get(mb, c + MESH_P_EXIT);
            mesh_buf_printf(&kind, "kind native pid %u cpu ", mesh_mailbox_get(mb, c + MESH_P_PID));
            mesh_rs_fixed(&kind, cpu, 2);
            mesh_buf_puts(&kind, " rss_mb ");
            mesh_rs_fixed(&kind, rss, 1);
            mesh_buf_printf(&kind, " spawns %u crashes %u exit %u/%u limits ", mesh_mailbox_get(mb, c + MESH_P_SPAWNS),
                            mesh_mailbox_get(mb, c + MESH_P_CRASHES), exit_ >> 8, exit_ & 0xffu);
            hexalt(&kind, mesh_mailbox_get(mb, c + MESH_P_LIMITS));
        } else if (ex == MESH_BACKEND_WASM) {
            mesh_buf_printf(&kind, "kind wasm lanes %u cpu ", units);
            mesh_rs_fixed(&kind, cpu, 2);
            mesh_buf_puts(&kind, " rss_mb ");
            mesh_rs_fixed(&kind, rss, 1);
        } else {
            char *label = mesh_backend_label(mesh_mailbox_get(mb, MESH_H_BACKEND));
            mesh_buf_printf(&kind, "kind %s warps %u shared %u reg_cap %u", label, rd32(e, MESH_PT_WARPS), mesh_mailbox_get(mb, mesh_prog_ext(s) + MESH_PX_SHARED),
                            mesh_mailbox_get(mb, mesh_prog_ext(s) + MESH_PX_REG_CAP));
            free(label);
        }
        mesh_buf_printf(out, "%u %s %s %u %u %s ", s, name, st < MESH_STATES_LEN ? MESH_STATES[st] : "?", rd32(e, MESH_PT_NODES), rd32(e, MESH_PT_WARPS),
                        pol < MESH_POLICIES_LEN ? MESH_POLICIES[pol] : "?");
        mesh_rs_fixed(out, rate, 1);
        mesh_buf_printf(out, " %016llx %u ", (unsigned long long)hash, rd32(e, MESH_PT_REGS));
        hexalt(out, rd32(e, MESH_PT_VIEW_OFF));
        mesh_buf_putc(out, '+');
        hexalt(out, rd32(e, MESH_PT_VIEW_BYTES));
        mesh_buf_printf(out, " %s\n", mesh_buf_str(&kind));
        mesh_buf_free(&kind);
        uint8_t x[MESH_PX_BYTES];
        mesh_mailbox_read(mb, mesh_prog_ext(s), x, MESH_PX_BYTES);
        uint32_t nf = rd32(x, MESH_PX_FIELD_COUNT);
        if (nf > MESH_MAX_FIELDS) nf = MESH_MAX_FIELDS;
        mesh_buf_printf(out, "fields %s", name);
        for (uint32_t i = 0; i < nf; i++) {
            size_t f = MESH_PX_FIELDS + MESH_FIELD_BYTES * (size_t)i;
            uint32_t ty = rd32(x, f + MESH_FD_TYPE);
            mesh_buf_putc(out, ' ');
            cstr(out, x + f + MESH_FD_NAME, MESH_FD_NAME_MAX + 1);
            mesh_buf_printf(out, ":%s:", ty < MESH_FIELD_TYPES_LEN ? MESH_FIELD_TYPES[ty] : "?");
            hexalt(out, rd32(x, f + MESH_FD_OFF));
        }
        mesh_buf_printf(out, "\ncounters %s", name);
        for (size_t i = 0; i < MESH_COUNTERS; i++) {
            const uint8_t *cn = x + MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES * i;
            mesh_buf_putc(out, ' ');
            if (!cn[0]) mesh_buf_putc(out, '-');
            else cstr(out, cn, MESH_NAME_BYTES);
        }
        mesh_buf_putc(out, '\n');
    }
    free(host);
}

void mesh_control_cluster(mesh_shared *sh, mesh_buf *out) {
    uint8_t *h = malloc(MESH_SNAP_HOST);
    mesh_host_block_bytes(&sh->host, h);
    uint32_t ps = rd32(h, MESH_S_PEER_STATE), leader = rd32(h, MESH_S_LEADER);
    static const char *const states[3] = {"single", "no-leader", "up"};
    mesh_buf_printf(out, "cluster %u system %u state %s leader ", rd32(h, MESH_S_SYSTEMS), sh->me, ps < 3 ? states[ps] : "?");
    if (leader == MESH_PEER_NONE) mesh_buf_putc(out, '-');
    else mesh_buf_printf(out, "%u", leader);
    mesh_buf_puts(out, " up ");
    hexalt(out, rd32(h, MESH_S_UP_MASK));
    mesh_buf_puts(out, " linked ");
    hexalt(out, rd32(h, MESH_S_LINKED));
    mesh_buf_puts(out, " lock_wait_us ");
    mesh_rs_fixed(out, rdf(h, MESH_S_LOCK_WAIT_US), 1);
    mesh_buf_putc(out, '\n');
    if (!sh->peer) { free(h); return; }
    for (uint32_t k = 0; k < sh->n; k++) {
        size_t m = mesh_member(k), l = mesh_mload(k);
        uint32_t cc = rd32(h, m + MESH_M_CC), lf = rd32(h, l + MESH_ML_FLAGS), st = rd32(h, m + MESH_M_STATE);
        uint64_t at = rd64(h, l + MESH_ML_AT_MS);
        mesh_buf_printf(out, "member %u %s rtt_us ", k, st < MESH_MEMBER_STATES_LEN ? MESH_MEMBER_STATES[st] : "?");
        mesh_rs_fixed(out, rdf(h, m + MESH_M_RTT_US), 1);
        mesh_buf_puts(out, " wait_us ");
        mesh_rs_fixed(out, rdf(h, m + MESH_M_WAIT_US), 1);
        char *label = mesh_backend_label(rd32(h, m + MESH_M_BACKEND));
        mesh_buf_printf(out, " rounds %u round %llu cc %u.%u sms %u/%u blocks_per_sm %u backend %s arch ", rd32(h, m + MESH_M_ROUNDS),
                        (unsigned long long)rd64(h, m + MESH_M_ROUND), cc / 10, cc % 10, rd32(h, m + MESH_M_SMS), rd32(h, m + MESH_M_SMS_TOTAL),
                        rd32(h, m + MESH_M_BLOCKS_PER_SM), label);
        free(label);
        cstr(out, h + m + MESH_M_ARCH, 16);
        mesh_buf_putc(out, ' ');
        if (at == 0) mesh_buf_puts(out, "load_age_ms - gpu_share - cpu - psi_cpu - psi_mem - meshd_cpu - mem_avail_mb -");
        else {
            uint64_t up = mesh_host_block_uptime_ms(&sh->host);
            mesh_buf_printf(out, "load_age_ms %llu gpu_share ", (unsigned long long)(up > at ? up - at : 0));
            if (lf & MESH_LF_GPU) mesh_rs_fixed(out, rdf(h, l + MESH_ML_SHARE10), 3);
            else mesh_buf_putc(out, '-');
            mesh_buf_puts(out, " cpu ");
            mesh_rs_fixed(out, rdf(h, l + MESH_ML_CPU), 3);
            mesh_buf_puts(out, " psi_cpu ");
            if (lf & MESH_LF_PSI) mesh_rs_fixed(out, rdf(h, l + MESH_ML_PSI_CPU), 4);
            else mesh_buf_putc(out, '-');
            mesh_buf_puts(out, " psi_mem ");
            if (lf & MESH_LF_PSI) mesh_rs_fixed(out, rdf(h, l + MESH_ML_PSI_MEM), 4);
            else mesh_buf_putc(out, '-');
            mesh_buf_puts(out, " meshd_cpu ");
            mesh_rs_fixed(out, rdf(h, l + MESH_ML_MESHD_CPU), 3);
            mesh_buf_printf(out, " mem_avail_mb %u", rd32(h, l + MESH_ML_MEM_AVAIL_MB));
        }
        mesh_buf_puts(out, " host ");
        cstr(out, h + m + MESH_M_HOST, 32);
        mesh_buf_puts(out, " gpu ");
        cstr(out, h + m + MESH_M_GPU, 32);
        mesh_buf_putc(out, '\n');
    }
    free(h);
}

/* Read-only bytes of a program's view (from the table; any thread). */
static int peek(mesh_shared *sh, const char *name, uint32_t off, uint32_t len, mesh_buf *out, char **err) {
    uint32_t s;
    if (!mesh_pt_find(sh->mb, name, &s)) {
        char *q = debug_q(name);
        *err = mesh_buf_fmt("no program %s", q);
        free(q);
        return -1;
    }
    uint8_t e[MESH_PT_BYTES];
    mesh_pt_read_entry(sh->mb, s, e);
    uint32_t view = rd32(e, MESH_PT_VIEW_OFF), bytes = rd32(e, MESH_PT_VIEW_BYTES);
    if ((uint64_t)off + len > bytes || off % 4 != 0) {
        *err = mesh_buf_fmt("%s: 0x%x+0x%x is outside its 0x%x-byte view (offsets 4-aligned)", name, off, len, bytes);
        return -1;
    }
    size_t at = out->len;
    mesh_buf_zeros(out, len);
    mesh_mailbox_read(sh->mb, (size_t)view + off, (uint8_t *)out->data + at, len);
    return 0;
}

/* A byte request through a program's request window (OP_REQ); framed reply. */
static void request(mesh_shared *sh, const char *name, uint8_t *bytes, size_t n, mesh_buf *out) {
    mesh_reply *r = mesh_reply_new();
    mesh_cmd c = mesh_cmd_new(MESH_CMD_REQ);
    c.name = mesh_xstrdup(name);
    c.bytes = bytes;
    c.len = n;
    c.reply = mesh_reply_sender(r);
    if (mesh_chan_send(sh->cmd, &c) != MESH_CHAN_OK) {
        mesh_cmd_free(&c);
        mesh_reply_release(r);
        framed_str(out, "err driver gone\n");
        return;
    }
    mesh_result x;
    int w = mesh_reply_wait(r, TASK_MS, &x);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) { framed_str(out, "err the request did not complete in 10s\n"); return; }
    if (x.ok) framed(out, x.bytes ? x.bytes : (uint8_t *)"", x.len);
    else framed_err(out, x.text ? x.text : "");
    mesh_result_free(&x);
}

static void jobs_req(mesh_shared *sh, const char *cmd, const char *req, const char *first, mesh_buf *out) {
    bool read = strcmp(cmd, "jobs") == 0 || (first && (strcmp(first, "status") == 0 || strcmp(first, "results") == 0));
    mesh_reply *r = mesh_reply_new();
    mesh_job_event e = mesh_job_event_new(MESH_JEV_CTL);
    e.line = mesh_xstrdup(req);
    e.reply_to = MESH_JOB_REPLY_LOCAL;
    e.reply = mesh_reply_sender(r);
    if (mesh_chan_send(sh->jobs, &e) != MESH_CHAN_OK) {
        mesh_job_event_free(&e);
        mesh_reply_release(r);
        line(out, "err the jobs thread is gone");
        return;
    }
    int64_t within = read ? TASK_MS : FORWARD_WAIT_MS + 5000;
    mesh_result x;
    int w = mesh_reply_wait(r, within, &x);
    mesh_reply_release(r);
    char *text;
    bool ok;
    if (w != MESH_CHAN_OK) {
        mesh_buf b = MESH_BUF_INIT;
        mesh_buf_puts(&b, "jobs: no answer in ");
        mesh_rs_duration_ms(&b, (uint64_t)within);
        text = mesh_buf_take(&b);
        ok = false;
    } else {
        ok = x.ok;
        text = x.text ? x.text : mesh_xstrdup("");
        x.text = NULL;
        mesh_result_free(&x);
    }
    if (read) {
        if (ok) framed_str(out, text);
        else framed_err(out, text);
    } else reply(out, ok, text);
    free(text);
}

static void trace(mesh_shared *sh, mesh_buf *out) {
    mesh_buf b = MESH_BUF_INIT;
    size_t recs = (size_t)MESH_TRACE_BYTES * MESH_TRACE_PER_PROG;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (mesh_mailbox_get(sh->mb, mesh_prog_entry(s) + MESH_PT_STATE) == MESH_ST_EMPTY) continue;
        mesh_buf_u32le(&b, s);
        mesh_buf_u32le(&b, mesh_mailbox_get(sh->mb, mesh_prog_ctl(s) + MESH_P_TRACE_N));
        size_t at = b.len;
        mesh_buf_zeros(&b, recs);
        mesh_mailbox_read(sh->mb, mesh_trace_rec(s, 0), (uint8_t *)b.data + at, recs);
    }
    framed(out, b.data ? b.data : "", b.len);
    mesh_buf_free(&b);
}

/* ---------------------------------------------------------------- dispatch */

void mesh_control_handle(mesh_shared *sh, const char *req, mesh_control_body body, void *ctx, mesh_buf *out) {
    const char *sp = strchr(req, ' ');
    char *cmd = sp ? mesh_xstrndup(req, (size_t)(sp - req)) : mesh_xstrdup(req);
    /* arg: what follows the first space, trimmed. */
    const char *a = sp ? sp + 1 : "";
    while (*a && is_ws(*a)) a++;
    size_t an = strlen(a);
    while (an && is_ws(a[an - 1])) an--;
    char *arg = mesh_xstrndup(a, an);
    char *wcopy = mesh_xstrdup(arg);
    char *w[16];
    size_t nw = split_ws(wcopy, w, 16);
    bool peer = sh->peer != NULL;
#define IS(x) (strcmp(cmd, (x)) == 0)
#define W(i) ((i) < nw && (i) < 16 ? w[i] : NULL)
    if (IS("snap")) {
        mesh_buf s = MESH_BUF_INIT;
        mesh_snapshot(&s, sh->mb, &sh->host);
        framed(out, s.data, s.len);
        mesh_buf_free(&s);
    } else if (IS("programs")) {
        mesh_buf t = MESH_BUF_INIT;
        mesh_control_programs(sh, &t);
        framed(out, t.data ? t.data : "", t.len);
        mesh_buf_free(&t);
    } else if (IS("pipes")) {
        mesh_buf t = MESH_BUF_INIT;
        mesh_control_pipes(sh, &t);
        framed(out, t.data ? t.data : "", t.len);
        mesh_buf_free(&t);
    } else if (IS("view")) {
        mesh_buf t = MESH_BUF_INIT;
        char *err = NULL;
        const char *w0 = nw > 0 ? w[0] : "", *w1 = nw > 1 ? w[1] : "";
        if (mesh_control_view(sh, w0, strcmp(w1, "json") == 0, &t, &err) == 0) framed(out, t.data ? t.data : "", t.len);
        else { framed_err(out, err); free(err); }
        mesh_buf_free(&t);
    } else if (IS("trace")) {
        trace(sh, out);
    } else if (IS("catalog")) {
        char **names;
        size_t n;
        char *err = NULL;
        if (mesh_catalog(sh->programs, &names, &n, &err)) {
            mesh_buf t = MESH_BUF_INIT;
            for (size_t i = 0; i < n; i++) { mesh_buf_printf(&t, "%s\n", names[i]); free(names[i]); }
            free(names);
            framed(out, t.data ? t.data : "", t.len);
            mesh_buf_free(&t);
        } else { framed_err(out, err); free(err); }
    } else if ((IS("load") || IS("upgrade") || IS("rollback") || IS("unload") || IS("start") || IS("stop") || IS("reset") || IS("group")) && peer && nw) {
        change(sh, req, out);
    } else if (IS("train-reset") && peer) {
        change(sh, "reset train", out);
    } else if (IS("task") && peer && nw == 3 && lockstep(sh, w[0])) {
        change(sh, req, out);
    } else if (IS("place") && peer && nw >= 2) {
        change(sh, req, out);
    } else if (IS("fleet") && peer) {
        char *t = mesh_shared_text(sh, &sh->fleet_text);
        framed_str(out, t);
        free(t);
    } else if (IS("sched") && peer) {
        if (nw == 0) {
            char *t = mesh_shared_text(sh, &sh->sched_all);
            framed_str(out, t);
            free(t);
        } else if (nw == 2 && strcmp(w[0], "explain") == 0) {
            char *t = NULL;
            pthread_mutex_lock(&sh->mu);
            for (size_t i = 0; i < sh->nexplain; i++) if (strcmp(sh->sched_explain[i].name, w[1]) == 0) t = mesh_xstrdup(sh->sched_explain[i].text);
            pthread_mutex_unlock(&sh->mu);
            if (t) framed_str(out, t);
            else {
                char *q = debug_q(w[1]);
                char *e = mesh_buf_fmt("no program %s in the desired state", q);
                framed_err(out, e);
                free(e);
                free(q);
            }
            free(t);
        } else if (nw == 1 && strcmp(w[0], "dry-run") == 0) {
            char *t = mesh_shared_text(sh, &sh->sched_dry);
            framed_str(out, t);
            free(t);
        } else if ((nw == 3 && strcmp(w[0], "pin") == 0) || (nw == 2 && strcmp(w[0], "unpin") == 0) || (nw == 1 && strcmp(w[0], "pause") == 0) ||
                   (nw == 1 && strcmp(w[0], "resume") == 0) ||
                   (nw == 2 && strcmp(w[0], "autoreform") == 0 && (strcmp(w[1], "on") == 0 || strcmp(w[1], "off") == 0)) ||
                   (nw == 2 && strcmp(w[0], "cordon") == 0) || (nw == 2 && strcmp(w[0], "uncordon") == 0)) {
            change(sh, req, out);
        } else {
            line(out, "err usage: sched [explain <name> | dry-run | pin <name> <member[,member..]> | unpin <name> | pause | resume | autoreform on|off "
                      "| cordon <k> | uncordon <k>]");
        }
    } else if (IS("groups") && peer) {
        char *t = mesh_shared_text(sh, &sh->groups_text);
        framed_str(out, t);
        free(t);
    } else if (IS("leader") && peer) {
        uint8_t *h = malloc(MESH_SNAP_HOST);
        mesh_host_block_bytes(&sh->host, h);
        uint32_t l = rd32(h, MESH_S_LEADER);
        unsigned long long t = (unsigned long long)rd64(h, MESH_S_TERM);
        free(h);
        if (l == MESH_PEER_NONE) mesh_buf_printf(out, "err peer: no leader (term %llu): this member does not hear a majority\n", t);
        else mesh_buf_printf(out, "ok system %u term %llu\n", l, t);
    } else if (IS("fleet") || IS("groups") || IS("leader") || IS("group") || IS("sched")) {
        line(out, "err not in a fleet (single meshd: --cluster / --peer)");
    } else if (IS("jobs") || IS("job")) {
        jobs_req(sh, cmd, req, W(0), out);
    } else if (IS("infer")) {
        mesh_infer_request r;
        mesh_infer_msg res;
        char *err = NULL;
        if (mesh_infer_parse(arg, &r, &err) == 0 && mesh_inferd_run(sh, &r, &res, &err) == 0) {
            mesh_buf t = MESH_BUF_INIT;
            mesh_buf_puts(&t, "ok\n");
            mesh_infer_text(&t, &res);
            framed(out, t.data, t.len);
            mesh_buf_free(&t);
            mesh_infer_msg_free(&res);
        } else {
            framed_err(out, err);
            free(err);
        }
        mesh_infer_request_free(&r);
    } else if (IS("render")) {
        uint8_t *b = NULL;
        size_t n = 0;
        char *err = NULL;
        bool ok = mesh_control_render ? mesh_control_render(sh, w, nw < 16 ? nw : 16, &b, &n, &err) : (err = mesh_xstrdup("the blob thread is gone"), false);
        if (ok) framed(out, b ? b : (uint8_t *)"", n);
        else framed_err(out, err);
        free(b);
        free(err);
    } else if (IS("load") && nw == 1) {
        mesh_cmd c = mesh_cmd_new(MESH_CMD_LOAD);
        c.has_src = true;
        c.src = mesh_src_catalog(arg);
        ask(sh, c, 2 * LIFECYCLE_MS, out);
    } else if (IS("upgrade") && (nw == 1 || nw == 2)) {
        mesh_cmd c = mesh_cmd_new(MESH_CMD_UPGRADE);
        c.name = mesh_xstrdup(w[0]);
        if (nw == 2) { c.has_src = true; c.src = mesh_src_catalog(w[1]); }
        ask(sh, c, 2 * LIFECYCLE_MS, out);
    } else if (IS("rollback") && nw == 1) {
        mesh_cmd c = mesh_cmd_new(MESH_CMD_ROLLBACK);
        c.name = mesh_xstrdup(arg);
        ask(sh, c, 2 * LIFECYCLE_MS, out);
    } else if (IS("unload") && (nw == 1 || (nw == 2 && strcmp(w[1], "--now") == 0))) {
        mesh_cmd c = mesh_cmd_new(MESH_CMD_UNLOAD);
        c.name = mesh_xstrdup(w[0]);
        c.now = nw == 2;
        ask(sh, c, 2 * LIFECYCLE_MS, out);
    } else if (IS("reset") || IS("train-reset")) {
        mesh_cmd c = mesh_cmd_new(MESH_CMD_RESET);
        c.name = mesh_xstrdup(IS("train-reset") ? "train" : arg);
        ask(sh, c, 2 * LIFECYCLE_MS, out);
    } else if ((IS("start") || IS("stop")) && nw == 1) {
        mesh_cmd c = mesh_cmd_new(MESH_CMD_PROG_RUN);
        c.name = mesh_xstrdup(arg);
        c.on = IS("start");
        ask(sh, c, 2 * LIFECYCLE_MS, out);
    } else if (IS("task")) {
        uint32_t op, ta;
        if (nw != 3 || !int_(w[1], &op) || !int_(w[2], &ta)) line(out, "err usage: task <name> <op> <arg>");
        else {
            mesh_reply *r = mesh_reply_new();
            mesh_cmd c = mesh_cmd_new(MESH_CMD_TASK);
            c.name = mesh_xstrdup(w[0]);
            c.op = op;
            c.arg = ta;
            c.reply = mesh_reply_sender(r);
            if (mesh_chan_send(sh->cmd, &c) != MESH_CHAN_OK) { mesh_cmd_free(&c); line(out, "err driver gone"); }
            else {
                mesh_result x;
                int got = mesh_reply_wait(r, TASK_MS, &x);
                if (got != MESH_CHAN_OK) line(out, "err the task did not complete in 10s");
                else if (x.ok) mesh_buf_printf(out, "ok %u\n", x.value);
                else mesh_buf_printf(out, "err %s\n", x.text ? x.text : "");
                if (got == MESH_CHAN_OK) mesh_result_free(&x);
            }
            mesh_reply_release(r);
        }
    } else if (IS("req")) {
        uint64_t len = 0;
        if (nw != 2 || mesh_rs_parse_u64(w[1], strlen(w[1]), &len) != 0) framed_str(out, "err usage: req <name> <len>, then the bytes\n");
        else if (len > REQ_MAX) framed_str(out, "err req: at most 1048576 bytes\n");
        else {
            uint8_t *bytes = mesh_xmalloc(len ? len : 1);
            if (!body || !body(ctx, bytes, len)) { free(bytes); framed_str(out, "err req: short body\n"); }
            else if (lockstep(sh, w[0])) {
                free(bytes);
                char *e = mesh_buf_fmt("%s: the lockstep program takes no requests in a fleet (they would not reach its group)", w[0]);
                framed_err(out, e);
                free(e);
            } else request(sh, w[0], bytes, len, out);
        }
    } else if (IS("input") || IS("key")) {
        const char *name, *bytes;
        char *nm = NULL;
        if (IS("key")) { name = "doom"; bytes = arg; }
        else {
            const char *s2 = strchr(arg, ' ');
            if (s2) { nm = mesh_xstrndup(arg, (size_t)(s2 - arg)); name = nm; bytes = s2 + 1; }
            else { name = arg; bytes = ""; }
        }
        if (!*bytes) mesh_buf_printf(out, "err usage: %s\n", IS("key") ? "key <k>" : "input <name> <bytes>");
        else if (lockstep(sh, name))
            mesh_buf_printf(out, "err %s: the lockstep program takes no input in a fleet (it would not reach its group)\n", name);
        else {
            mesh_cmd c = mesh_cmd_new(MESH_CMD_INPUT);
            c.name = mesh_xstrdup(name);
            c.len = strlen(bytes);
            c.bytes = (uint8_t *)mesh_xstrdup(bytes);
            ask(sh, c, TASK_MS, out);
        }
        free(nm);
    } else if (IS("place")) {
        if (!nw) line(out, "err usage: place <name> [local | pin <k> | split [auto | a,b,.. | a:n,b:m,..]]");
        else {
            mesh_cmd c = mesh_cmd_new(MESH_CMD_PLACE);
            c.name = mesh_xstrdup(w[0]);
            if (nw > 1) {
                c.has_words = true;
                c.nwords = nw - 1;
                c.words = mesh_xcalloc(nw, sizeof *c.words);
                for (size_t i = 1; i < nw && i < 16; i++) c.words[i - 1] = mesh_xstrdup(w[i]);
            }
            ask(sh, c, PLACE_MS, out);
        }
    } else if (IS("peek")) {
        uint32_t off, len;
        if (nw == 3 && int_(w[1], &off) && int_(w[2], &len)) {
            mesh_buf t = MESH_BUF_INIT;
            char *err = NULL;
            if (peek(sh, w[0], off, len, &t, &err) == 0) framed(out, t.data ? t.data : "", t.len);
            else { framed_err(out, err); free(err); }
            mesh_buf_free(&t);
        } else framed_str(out, "err usage: peek <name> <off> <len>\n");
    } else if (IS("cluster")) {
        mesh_buf t = MESH_BUF_INIT;
        mesh_control_cluster(sh, &t);
        framed(out, t.data, t.len);
        mesh_buf_free(&t);
    } else if (IS("load") && nw == 0) {
        mesh_buf t = MESH_BUF_INIT;
        mesh_load_text(sh, &t);
        framed(out, t.data, t.len);
        mesh_buf_free(&t);
    } else if (IS("log")) {
        uint64_t n = 50;
        if (*arg && mesh_rs_parse_u64(arg, strlen(arg), &n) != 0) n = 50;
        char **lines;
        size_t k = mesh_log_recent((size_t)n, &lines);
        mesh_buf t = MESH_BUF_INIT;
        for (size_t i = 0; i < k; i++) { mesh_buf_printf(&t, "%s\n", lines[i]); free(lines[i]); }
        free(lines);
        framed(out, t.data ? t.data : "", t.len);
        mesh_buf_free(&t);
    } else if (IS("http")) {
        const char *path = *arg ? arg : "/";
        char *head = mesh_buf_fmt("GET %s HTTP/1.0\r\n\r\n", path);
        uint8_t *resp;
        size_t rn;
        char *err = NULL;
        if (mesh_shared_http(sh, (uint8_t *)head, strlen(head), &resp, &rn, &err)) { framed(out, resp, rn); free(resp); }
        else {
            mesh_error("control http %s: %s", path, err);
            framed_err(out, err);
            free(err);
        }
        free(head);
    } else if (!*cmd) {
        line(out, "err empty request");
    } else {
        char *q = debug_q(req);
        mesh_info("control: unknown or malformed request %s", q);
        mesh_buf_printf(out, "err unknown or malformed request %s\n", q);
        free(q);
    }
#undef IS
#undef W
    free(wcopy);
    free(arg);
    free(cmd);
}

/* ---------------------------------------------------------------- the client loop */

/* The client's socket, buffered, counting the bytes read from it (S_CTL_RX). */
typedef struct {
    int fd;
    uint8_t buf[8192];
    size_t a, b;
    uint64_t n;
} creader;

static bool fill(creader *r) {
    if (r->a < r->b) return true;
    for (;;) {
        ssize_t k = read(r->fd, r->buf, sizeof r->buf);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return false;
        r->a = 0;
        r->b = (size_t)k;
        r->n += (uint64_t)k;
        return true;
    }
}

/* read_line under take(max): bytes up to and including '\n', at most max. -1: error or EOF with nothing. */
static long read_line(creader *r, mesh_buf *l, size_t max) {
    mesh_buf_clear(l);
    while (l->len < max) {
        if (!fill(r)) return l->len ? (long)l->len : -1;
        uint8_t c = r->buf[r->a++];
        mesh_buf_putc(l, (char)c);
        if (c == '\n') break;
    }
    return (long)l->len;
}

static bool body_read(void *ctx, uint8_t *out, size_t n) {
    creader *r = ctx;
    size_t got = 0;
    while (got < n) {
        if (!fill(r)) return false;
        size_t k = r->b - r->a < n - got ? r->b - r->a : n - got;
        memcpy(out + got, r->buf + r->a, k);
        r->a += k;
        got += k;
    }
    return true;
}

static bool write_all(int fd, const char *p, size_t n) {
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

static void timeouts(int fd, int64_t idle_ms) {
    struct timeval rt = {(time_t)(idle_ms / 1000), (suseconds_t)(idle_ms % 1000) * 1000}, wt = {CLIENT_WRITE_MS / 1000, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof rt);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &wt, sizeof wt);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

void mesh_control_client(mesh_shared *sh, int fd, int64_t idle_ms) {
    timeouts(fd, idle_ms);
    creader *r = mesh_xcalloc(1, sizeof *r);
    r->fd = fd;
    mesh_buf l = MESH_BUF_INIT, out = MESH_BUF_INIT;
    uint64_t counted = 0;
    for (;;) {
        long n = read_line(r, &l, LINE_MAX + 1);
        if (n <= 0 || !mesh_rs_utf8_valid(l.data, l.len)) break;
        if (l.data[l.len - 1] != '\n' && l.len > LINE_MAX) {
            mesh_error("control: a request line over %d bytes; closing that client", LINE_MAX);
            const char *e = "err request line over 4096 bytes\n";
            if (!write_all(fd, e, strlen(e))) {}
            break;
        }
        /* trim */
        const char *s = l.data;
        size_t k = l.len;
        while (k && is_ws(*s)) { s++; k--; }
        while (k && is_ws(s[k - 1])) k--;
        char *req = mesh_xstrndup(s, k);
        mesh_buf_clear(&out);
        mesh_control_handle(sh, req, body_read, r, &out);
        free(req);
        bool ok = write_all(fd, out.data, out.len);
        /* A request: the bytes its line and body took off the socket (read ahead included), its reply. */
        mesh_net_counters_ctl(&sh->host.net, (size_t)(r->n - counted), ok ? out.len : 0);
        counted = r->n;
        if (!ok) break;
    }
    mesh_buf_free(&l);
    mesh_buf_free(&out);
    free(r);
    close(fd);
}

typedef struct {
    mesh_shared *sh;
    int fd;
} client_arg;

static _Atomic int clients;

static void *client_thread(void *p) {
    client_arg *a = p;
    mesh_control_client(a->sh, a->fd, CLIENT_IDLE_MS);
    atomic_fetch_sub(&clients, 1);
    free(a);
    return NULL;
}

void mesh_control_serve(mesh_shared *sh, int listen_fd) {
    for (;;) {
        int c = accept(listen_fd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            mesh_error("control accept: %s (os error %d)", strerror(errno), errno);
            if (errno == EBADF || errno == EINVAL) return;
            continue;
        }
        if (atomic_load(&clients) >= MAX_CLIENTS) {
            mesh_error("control: %d clients connected already; refusing one", MAX_CLIENTS);
            const char *e = "err too many control clients\n";
            if (!write_all(c, e, strlen(e))) {}
            close(c);
            continue;
        }
        atomic_fetch_add(&clients, 1);
        client_arg *a = mesh_xmalloc(sizeof *a);
        a->sh = sh;
        a->fd = c;
        pthread_t th;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, client_thread, a) != 0) {
            atomic_fetch_sub(&clients, 1);
            close(c);
            free(a);
        }
        pthread_attr_destroy(&at);
    }
}
