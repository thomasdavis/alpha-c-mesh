/* crates/mesh-top/src/snap.rs, ported. */
#include "mesh/top_snap.h"

#include "tui/utf8.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- byte helpers */

uint32_t mt_get_u32(const uint8_t *b, size_t len, size_t at) {
    if (at + 4 > len || at + 4 < at) return 0;
    return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24;
}

uint64_t mt_get_u64(const uint8_t *b, size_t len, size_t at) {
    if (at + 8 > len || at + 8 < at) return 0;
    return (uint64_t)mt_get_u32(b, len, at) | (uint64_t)mt_get_u32(b, len, at + 4) << 32;
}

float mt_get_f32(const uint8_t *b, size_t len, size_t at) {
    uint32_t u = mt_get_u32(b, len, at);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

void mt_get_str(const uint8_t *b, size_t len, size_t at, size_t n, char *out) {
    out[0] = 0;
    if (at + n > len || at + n < at) return;
    size_t end = 0;
    while (end < n && b[at + end]) end++;
    size_t m;
    char *s = tui_utf8_lossy(b + at, end, &m);
    memcpy(out, s, m + 1);
    free(s);
}

void mt_put_u32(uint8_t *b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i));
}

void mt_put_u64(uint8_t *b, size_t at, uint64_t v) {
    for (int i = 0; i < 8; i++) b[at + i] = (uint8_t)(v >> (8 * i));
}

void mt_put_f32(uint8_t *b, size_t at, float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    mt_put_u32(b, at, u);
}

void mt_put_str(uint8_t *b, size_t at, size_t n, const char *s) {
    memset(b + at, 0, n);
    size_t k = strlen(s);
    if (k > n - 1) k = n - 1;
    memcpy(b + at, s, k);
}

/* ---- sections */

mt_section mt_section_new(uint32_t slot, uint32_t nodes, uint32_t view) {
    mt_section s;
    s.len = mesh_section_bytes(nodes, view);
    s.bytes = calloc(1, s.len);
    if (!s.bytes) abort();
    mt_put_u32(s.bytes, MESH_SC_SLOT, slot);
    mt_put_u32(s.bytes, MESH_SC_BYTES, (uint32_t)s.len);
    mt_put_u32(s.bytes, MESH_SC_NODES, nodes);
    mt_put_u32(s.bytes, MESH_SC_VIEW, view);
    return s;
}

void mt_section_free(mt_section *s) {
    free(s->bytes);
    s->bytes = NULL;
    s->len = 0;
}

mt_section mt_section_clone(const mt_section *s) {
    mt_section c = {malloc(s->len ? s->len : 1), s->len};
    if (!c.bytes) abort();
    memcpy(c.bytes, s->bytes, s->len);
    return c;
}

uint32_t mt_section_slot(const mt_section *s) { return mt_get_u32(s->bytes, s->len, MESH_SC_SLOT); }
size_t mt_section_nodes(const mt_section *s) { return mt_get_u32(s->bytes, s->len, MESH_SC_NODES); }
uint8_t *mt_section_entry(const mt_section *s) { return s->bytes + MESH_SC_ENTRY; }
uint8_t *mt_section_ctl(const mt_section *s) { return s->bytes + MESH_SC_CTL; }
uint8_t *mt_section_ext(const mt_section *s) { return s->bytes + MESH_SC_EXT; }
uint8_t *mt_section_node_rec(const mt_section *s, size_t n) { return s->bytes + MESH_SC_NODE_RECS + MESH_NODE_STRIDE * n; }
uint8_t *mt_section_view(const mt_section *s) { return s->bytes + MESH_SC_NODE_RECS + MESH_NODE_STRIDE * mt_section_nodes(s); }
size_t mt_section_view_len(const mt_section *s) { return mt_get_u32(s->bytes, s->len, MESH_SC_VIEW); }

void mt_section_name(const mt_section *s, char *out) {
    mt_get_str(mt_section_entry(s), MESH_PT_BYTES, MESH_PT_NAME, MESH_NAME_BYTES, out);
}

bool mt_section_name_is(const mt_section *s, const char *name) {
    char n[3 * MESH_NAME_BYTES + 1];
    mt_section_name(s, n);
    return strcmp(n, name) == 0;
}

uint32_t mt_section_pt(const mt_section *s, size_t off) { return mt_get_u32(mt_section_entry(s), MESH_PT_BYTES, off); }
uint32_t mt_section_state(const mt_section *s) { return mt_section_pt(s, MESH_PT_STATE); }
const char *mt_section_state_name(const mt_section *s) {
    uint32_t st = mt_section_state(s);
    return st < MESH_STATES_LEN ? MESH_STATES[st] : "?";
}
bool mt_section_running(const mt_section *s) { return mt_section_state(s) == MESH_ST_RUNNING; }
uint64_t mt_section_ptx_hash(const mt_section *s) { return mt_get_u64(mt_section_entry(s), MESH_PT_BYTES, MESH_PT_PTX_HASH); }
uint32_t mt_section_p(const mt_section *s, size_t off) { return mt_get_u32(mt_section_ctl(s), MESH_SC_CTL_BYTES, off); }
uint64_t mt_section_p64(const mt_section *s, size_t off) { return mt_get_u64(mt_section_ctl(s), MESH_SC_CTL_BYTES, off); }
bool mt_section_native(const mt_section *s) {
    return mt_get_u32(mt_section_ext(s), MESH_PX_BYTES, MESH_PX_EXEC) == MESH_BACKEND_NATIVE;
}
uint32_t mt_section_counter(const mt_section *s, size_t i) { return mt_section_p(s, MESH_P_COUNTERS + 4 * i); }
void mt_section_counter_name(const mt_section *s, size_t i, char *out) {
    mt_get_str(mt_section_ext(s), MESH_PX_BYTES, MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES * i, MESH_NAME_BYTES, out);
}

size_t mt_section_fields(const mt_section *s, mt_field *out) {
    const uint8_t *x = mt_section_ext(s);
    size_t n = mt_get_u32(x, MESH_PX_BYTES, MESH_PX_FIELD_COUNT);
    if (n > MESH_MAX_FIELDS) n = MESH_MAX_FIELDS;
    for (size_t i = 0; i < n; i++) {
        size_t at = MESH_PX_FIELDS + MESH_FIELD_BYTES * i;
        mt_get_str(x, MESH_PX_BYTES, at + MESH_FD_NAME, MESH_FD_NAME_MAX + 1, out[i].name);
        out[i].ty = mt_get_u32(x, MESH_PX_BYTES, at + MESH_FD_TYPE);
        out[i].off = mt_get_u32(x, MESH_PX_BYTES, at + MESH_FD_OFF);
    }
    return n;
}

void mt_section_field_value(const mt_section *s, const mt_field *f, char *out, size_t cap) {
    const uint8_t *v = mt_section_view(s);
    size_t vl = mt_section_view_len(s);
    size_t size = f->ty == MESH_FT_U64 ? 8 : f->ty == MESH_FT_U8 ? 1 : 4;
    if (f->off + size > vl) {
        snprintf(out, cap, "-");
        return;
    }
    switch (f->ty) {
    case MESH_FT_U32: snprintf(out, cap, "%u", mt_get_u32(v, vl, f->off)); break;
    case MESH_FT_I32: snprintf(out, cap, "%d", (int32_t)mt_get_u32(v, vl, f->off)); break;
    case MESH_FT_F32: {
        float x = mt_get_f32(v, vl, f->off);
        if (isnan(x)) snprintf(out, cap, "NaN");
        else if (isinf(x)) snprintf(out, cap, x < 0 ? "-inf" : "inf");
        else snprintf(out, cap, "%.6f", (double)x);
        break;
    }
    case MESH_FT_U64: snprintf(out, cap, "%llu", (unsigned long long)mt_get_u64(v, vl, f->off)); break;
    case MESH_FT_U8: snprintf(out, cap, "%u", v[f->off]); break;
    default: snprintf(out, cap, "?type%u", f->ty); break;
    }
}

mt_node mt_section_node(const mt_section *s, size_t n) {
    mt_node r = {0, 0, 0, 0, 0};
    if (n >= mt_section_nodes(s)) return r;
    size_t at = MESH_SC_NODE_RECS + MESH_NODE_STRIDE * n;
    r.sm = mt_get_u32(s->bytes, s->len, at + MESH_N_SM);
    r.heartbeat = mt_get_u32(s->bytes, s->len, at + MESH_N_HEARTBEAT);
    r.tasks = mt_get_u32(s->bytes, s->len, at + MESH_N_TASKS);
    r.last_op = mt_get_u32(s->bytes, s->len, at + MESH_N_LAST_OP);
    r.busy_ns = mt_get_u64(s->bytes, s->len, at + MESH_N_BUSY_NS);
    return r;
}

uint32_t mt_section_view_u32(const mt_section *s, size_t off) { return mt_get_u32(mt_section_view(s), mt_section_view_len(s), off); }
float mt_section_view_f32(const mt_section *s, size_t off) { return mt_get_f32(mt_section_view(s), mt_section_view_len(s), off); }

/* ---- snapshots */

static char *fmt(const char *f, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
static char *fmt(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    char tmp[256];
    vsnprintf(tmp, sizeof tmp, f, ap);
    va_end(ap);
    char *s = malloc(strlen(tmp) + 1);
    if (!s) abort();
    strcpy(s, tmp);
    return s;
}

bool mt_snapshot_parse(const uint8_t *bytes, size_t len, mt_snapshot *out, char **err) {
    size_t min = MESH_SN_HEAD_BYTES + MESH_H_BYTES + MESH_SNAP_HOST;
    *out = (mt_snapshot){NULL, 0, NULL, 0};
    if (len < min) {
        *err = fmt("snapshot is %zu bytes, v%u needs at least %zu", len, (unsigned)MESH_SNAP_VERSION, min);
        return false;
    }
    size_t total = mt_get_u32(bytes, len, MESH_SN_TOTAL), count = mt_get_u32(bytes, len, MESH_SN_COUNT);
    uint32_t version = mt_get_u32(bytes, len, MESH_SN_VERSION);
    size_t host_at = mt_get_u32(bytes, len, MESH_SN_HOST_AT);
    if (version != MESH_SNAP_VERSION) {
        *err = fmt("snapshot version %u, this viewer speaks %u", version, (unsigned)MESH_SNAP_VERSION);
        return false;
    }
    if (total != len || host_at + MESH_SNAP_HOST != total) {
        *err = fmt("snapshot framing: total %zu, host at %zu, received %zu", total, host_at, len);
        return false;
    }
    if (count > MESH_PROG_SLOTS) {
        *err = fmt("snapshot has %zu sections, at most %u", count, (unsigned)MESH_PROG_SLOTS);
        return false;
    }
    mt_section *secs = calloc(count ? count : 1, sizeof *secs);
    if (!secs) abort();
    size_t at = MESH_SN_HEAD_BYTES + MESH_H_BYTES, n = 0;
    for (size_t i = 0; i < count; i++) {
        if (at + MESH_SC_HEAD_BYTES > host_at) {
            *err = fmt("section %zu starts past the host block", i);
            goto fail;
        }
        uint32_t slot = mt_get_u32(bytes, len, at + MESH_SC_SLOT);
        size_t sl = mt_get_u32(bytes, len, at + MESH_SC_BYTES);
        uint32_t nodes = mt_get_u32(bytes, len, at + MESH_SC_NODES), view = mt_get_u32(bytes, len, at + MESH_SC_VIEW);
        if (slot >= MESH_PROG_SLOTS || (n > 0 && mt_section_slot(&secs[n - 1]) >= slot)) {
            *err = fmt("section %zu: slot %u out of range or out of order", i, slot);
            goto fail;
        }
        if (nodes > MESH_PROG_MAX_NODES || view > MESH_VIEW_HEAP_END - MESH_VIEW_HEAP) {
            *err = fmt("section %zu: %u nodes, %u view bytes", i, nodes, view);
            goto fail;
        }
        if (sl != mesh_section_bytes(nodes, view) || at + sl > host_at) {
            *err = fmt("section %zu: %zu bytes, the layout says %zu", i, sl, mesh_section_bytes(nodes, view));
            goto fail;
        }
        secs[n].bytes = malloc(sl);
        if (!secs[n].bytes) abort();
        memcpy(secs[n].bytes, bytes + at, sl);
        secs[n].len = sl;
        n++;
        at += sl;
    }
    if (at != host_at) {
        *err = fmt("%zu stray bytes before the host block", host_at - at);
        goto fail;
    }
    out->bytes = malloc(len);
    if (!out->bytes) abort();
    memcpy(out->bytes, bytes, len);
    out->len = len;
    out->sections = secs;
    out->nsections = n;
    return true;
fail:
    for (size_t k = 0; k < n; k++) free(secs[k].bytes);
    free(secs);
    return false;
}

static int by_slot(const void *a, const void *b) {
    uint32_t x = mt_section_slot(a), y = mt_section_slot(b);
    return x < y ? -1 : x > y;
}

mt_snapshot mt_snapshot_assemble(const uint8_t *head, const mt_section *sections, size_t n, const uint8_t *host) {
    mt_section *sorted = malloc((n ? n : 1) * sizeof *sorted);
    if (!sorted) abort();
    if (n) memcpy(sorted, sections, n * sizeof *sorted);
    /* A stable sort by slot, as Rust's sort_by_key. */
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && by_slot(&sorted[j - 1], &sorted[j]) > 0; j--) {
            mt_section t = sorted[j];
            sorted[j] = sorted[j - 1];
            sorted[j - 1] = t;
        }
    size_t len = MESH_SN_HEAD_BYTES + MESH_H_BYTES + MESH_SNAP_HOST;
    for (size_t i = 0; i < n; i++) len += sorted[i].len;
    uint8_t *b = calloc(1, len);
    if (!b) abort();
    memcpy(b + MESH_SN_HEAD_BYTES, head, MESH_H_BYTES);
    size_t at = MESH_SN_HEAD_BYTES + MESH_H_BYTES;
    for (size_t i = 0; i < n; i++) {
        memcpy(b + at, sorted[i].bytes, sorted[i].len);
        at += sorted[i].len;
    }
    size_t host_at = at;
    memcpy(b + at, host, MESH_SNAP_HOST);
    mt_put_u32(b, MESH_SN_TOTAL, (uint32_t)len);
    mt_put_u32(b, MESH_SN_COUNT, (uint32_t)n);
    mt_put_u32(b, MESH_SN_VERSION, MESH_SNAP_VERSION);
    mt_put_u32(b, MESH_SN_HOST_AT, (uint32_t)host_at);
    free(sorted);
    mt_snapshot s;
    char *err = NULL;
    if (!mt_snapshot_parse(b, len, &s, &err)) {
        fprintf(stderr, "assembled snapshot does not parse: %s\n", err);
        abort();
    }
    free(b);
    return s;
}

mt_snapshot mt_snapshot_clone(const mt_snapshot *s) {
    mt_snapshot c;
    char *err = NULL;
    if (!mt_snapshot_parse(s->bytes, s->len, &c, &err)) abort();
    return c;
}

void mt_snapshot_free(mt_snapshot *s) {
    for (size_t i = 0; i < s->nsections; i++) free(s->sections[i].bytes);
    free(s->sections);
    free(s->bytes);
    *s = (mt_snapshot){NULL, 0, NULL, 0};
}

const mt_section *mt_snapshot_by_name(const mt_snapshot *s, const char *name) {
    for (size_t i = 0; i < s->nsections; i++)
        if (mt_section_name_is(&s->sections[i], name)) return &s->sections[i];
    return NULL;
}

const mt_section *mt_snapshot_by_slot(const mt_snapshot *s, uint32_t slot) {
    for (size_t i = 0; i < s->nsections; i++)
        if (mt_section_slot(&s->sections[i]) == slot) return &s->sections[i];
    return NULL;
}

uint32_t mt_snapshot_nodes_online(const mt_snapshot *s) {
    uint32_t n = 0;
    for (size_t i = 0; i < s->nsections; i++) n += mt_section_p(&s->sections[i], MESH_P_ONLINE);
    return n;
}

uint32_t mt_snapshot_head(const mt_snapshot *s, size_t off) { return mt_get_u32(s->bytes, s->len, MESH_SN_HEAD_BYTES + off); }
const uint8_t *mt_snapshot_host(const mt_snapshot *s) { return s->bytes + s->len - MESH_SNAP_HOST; }
uint32_t mt_snapshot_host_u32(const mt_snapshot *s, size_t off) { return mt_get_u32(mt_snapshot_host(s), MESH_SNAP_HOST, off); }
uint64_t mt_snapshot_host_u64(const mt_snapshot *s, size_t off) { return mt_get_u64(mt_snapshot_host(s), MESH_SNAP_HOST, off); }
float mt_snapshot_host_f32(const mt_snapshot *s, size_t off) { return mt_get_f32(mt_snapshot_host(s), MESH_SNAP_HOST, off); }
void mt_snapshot_host_str(const mt_snapshot *s, size_t off, size_t n, char *out) {
    mt_get_str(mt_snapshot_host(s), MESH_SNAP_HOST, off, n, out);
}
void mt_snapshot_hostname(const mt_snapshot *s, char out[97]) { mt_snapshot_host_str(s, MESH_S_HOSTNAME, 32, out); }
void mt_snapshot_gpu(const mt_snapshot *s, char out[97]) { mt_snapshot_host_str(s, MESH_S_GPU, 32, out); }

float mt_snapshot_rate(const mt_snapshot *s, uint32_t slot) {
    float r = mt_snapshot_host_f32(s, MESH_S_RATES + 4 * (size_t)slot);
    return isfinite(r) && r > 0.0f ? r : 0.0f;
}

bool mt_snapshot_system(const mt_snapshot *s, uint32_t *k) {
    uint32_t v = mt_snapshot_host_u32(s, MESH_S_SYSTEM);
    if (v == MESH_PEER_NONE) return false;
    if (k) *k = v;
    return true;
}

uint32_t mt_snapshot_systems(const mt_snapshot *s) {
    uint32_t n = mt_snapshot_host_u32(s, MESH_S_SYSTEMS);
    return n < MESH_MAX_SYSTEMS ? n : MESH_MAX_SYSTEMS;
}

uint64_t mt_snapshot_uptime_ms(const mt_snapshot *s) { return mt_snapshot_host_u64(s, MESH_S_UPTIME_MS); }

mt_member mt_snapshot_member(const mt_snapshot *s, uint32_t k) {
    const uint8_t *h = mt_snapshot_host(s) + mesh_member(k);
    size_t n = MESH_MEMBER_BYTES;
    mt_member m;
    m.system = k;
    m.state = mt_get_u32(h, n, MESH_M_STATE);
    m.cc = mt_get_u32(h, n, MESH_M_CC);
    m.sms = mt_get_u32(h, n, MESH_M_SMS);
    m.sms_total = mt_get_u32(h, n, MESH_M_SMS_TOTAL);
    m.blocks_per_sm = mt_get_u32(h, n, MESH_M_BLOCKS_PER_SM);
    m.rtt_us = mt_get_f32(h, n, MESH_M_RTT_US);
    m.wait_us = mt_get_f32(h, n, MESH_M_WAIT_US);
    m.rounds = mt_get_u32(h, n, MESH_M_ROUNDS);
    m.round = mt_get_u64(h, n, MESH_M_ROUND);
    mt_get_str(h, n, MESH_M_GPU, 32, m.gpu);
    mt_get_str(h, n, MESH_M_ARCH, 16, m.arch);
    mt_get_str(h, n, MESH_M_HOST, 32, m.host);
    m.backend = mt_get_u32(h, n, MESH_M_BACKEND);
    return m;
}

size_t mt_snapshot_members(const mt_snapshot *s, mt_member *out) {
    if (!mt_snapshot_system(s, NULL)) return 0;
    uint32_t n = mt_snapshot_systems(s);
    for (uint32_t k = 0; k < n; k++) out[k] = mt_snapshot_member(s, k);
    return n;
}

bool mt_snapshot_fleet(const mt_snapshot *s, mt_fleet *f) {
    if (!mt_snapshot_system(s, NULL)) return false;
    const uint8_t *h = mt_snapshot_host(s);
    size_t hl = MESH_SNAP_HOST;
    memset(f, 0, sizeof *f);
    uint32_t leader = mt_get_u32(h, hl, MESH_S_LEADER);
    f->has_leader = leader != MESH_PEER_NONE;
    f->leader = leader;
    uint32_t n = mt_snapshot_systems(s);
    f->nmembers = n;
    for (uint32_t k = 0; k < n; k++) {
        size_t at = mesh_fmember(k);
        f->members[k].status = mt_get_u32(h, hl, at + MESH_FM_STATUS);
        f->members[k].group = mt_get_u32(h, hl, at + MESH_FM_GROUP);
        f->members[k].applied = mt_get_u64(h, hl, at + MESH_FM_APPLIED);
        f->members[k].heard_ms = mt_get_u64(h, hl, at + MESH_FM_HEARD_MS);
        mt_get_str(h, hl, at + MESH_FM_REASON, 40, f->members[k].reason);
    }
    uint32_t gstate = mt_get_u32(h, hl, MESH_SG_STATE);
    if (gstate != 0) {
        f->has_group = true;
        mt_fleet_group *g = &f->group;
        mt_get_str(h, hl, MESH_SG_NAME, MESH_NAME_BYTES, g->name);
        g->epoch = mt_get_u32(h, hl, MESH_SG_EPOCH);
        g->cycle = mt_get_u32(h, hl, MESH_SG_CYCLE);
        g->mask = mt_get_u32(h, hl, MESH_SG_MASK);
        memcpy(g->order, h + MESH_SG_ORDER, MESH_MAX_SYSTEMS);
        g->running = gstate == 1;
        g->local = mt_get_u32(h, hl, MESH_SG_LOCAL);
        g->rank = mt_get_u32(h, hl, MESH_SG_RANK);
        g->reform = mt_get_u32(h, hl, MESH_SG_ON_LOSS) == 1;
        g->auto_ms = mt_get_u32(h, hl, MESH_SG_AUTO_MS);
    }
    f->role = mt_get_u32(h, hl, MESH_S_ROLE);
    f->term = mt_get_u64(h, hl, MESH_S_TERM);
    f->version = mt_get_u64(h, hl, MESH_S_STATE_VER);
    f->applied = mt_get_u64(h, hl, MESH_S_APPLIED_VER);
    f->up = mt_get_u32(h, hl, MESH_S_UP_MASK);
    f->suspect = mt_get_u32(h, hl, MESH_S_SUSPECT_MASK);
    f->quorum = mt_get_u32(h, hl, MESH_S_QUORUM);
    f->flags = mt_get_u32(h, hl, MESH_S_FLEET_FLAGS);
    f->dead_ms = mt_get_u32(h, hl, MESH_S_DEAD_MS);
    return true;
}

mt_jobs mt_snapshot_jobs(const mt_snapshot *s) {
    const uint8_t *h = mt_snapshot_host(s);
    size_t hl = MESH_SNAP_HOST;
    return (mt_jobs){mt_get_u32(h, hl, MESH_S_JOBS_ACTIVE), mt_get_u32(h, hl, MESH_S_JOBS_TOTAL),
                     mt_get_u64(h, hl, MESH_S_JOB_DONE),    mt_get_u64(h, hl, MESH_S_JOB_FAILED),
                     mt_get_u32(h, hl, MESH_S_JOB_LEASED),  mt_get_f32(h, hl, MESH_S_JOB_RATE),
                     mt_get_u64(h, hl, MESH_S_JOB_DUPES),   mt_get_u32(h, hl, MESH_S_JOB_FLAGS),
                     mt_get_u32(h, hl, MESH_S_JOB_HELD)};
}

static mt_opt_f32 opt(bool on, float v) { return (mt_opt_f32){on, on ? v : 0.0f}; }

bool mt_snapshot_load(const mt_snapshot *s, mt_load *l) {
    uint64_t at = mt_snapshot_host_u64(s, MESH_S_LOAD_AT_MS);
    if (at == 0) return false;
    memset(l, 0, sizeof *l);
    const uint8_t *h = mt_snapshot_host(s);
    uint32_t flags = mt_snapshot_host_u32(s, MESH_S_LOAD_FLAGS);
#define F(o) mt_snapshot_host_f32(s, (o))
#define U(o) mt_snapshot_host_u32(s, (o))
    size_t n = U(MESH_S_CPUS);
    if (n > MESH_MAX_CPUS) n = MESH_MAX_CPUS;
    uint64_t up = mt_snapshot_uptime_ms(s);
    l->at_ms = at;
    l->age_ms = up > at ? up - at : 0;
    l->samples = U(MESH_S_LOAD_SAMPLES);
    l->flags = flags;
    l->share = opt(flags & MESH_LF_GPU, F(MESH_S_GPU_SHARE));
    l->share10 = opt(flags & MESH_LF_GPU, F(MESH_S_GPU_SHARE10));
    l->slice_us = F(MESH_S_GPU_SLICE_US);
    l->gap_us = F(MESH_S_GPU_GAP_US);
    l->max_gap_us = F(MESH_S_GPU_MAX_GAP_US);
    l->gaps_s = F(MESH_S_GPU_GAPS_S);
    l->observed = F(MESH_S_GPU_OBS);
    l->util_ext = opt(flags & MESH_LF_GPU_EXT, F(MESH_S_GPU_UTIL_EXT));
    l->queue_us = F(MESH_S_GPU_QUEUE_US);
    l->inflate = opt(flags & MESH_LF_GPU, F(MESH_S_GPU_INFLATE));
    l->cpu = F(MESH_S_CPU_UTIL);
    l->cpu_big = F(MESH_S_CPU_BIG);
    l->cpu_little = F(MESH_S_CPU_LITTLE);
    l->iowait = F(MESH_S_CPU_IOWAIT);
    l->loadavg[0] = F(MESH_S_LOADAVG);
    l->loadavg[1] = F(MESH_S_LOADAVG + 4);
    l->loadavg[2] = F(MESH_S_LOADAVG + 8);
    l->cpus = U(MESH_S_CPUS);
    l->cpus_big = U(MESH_S_CPUS_BIG);
    l->cpus_little = U(MESH_S_CPUS_LITTLE);
    l->has_psi = flags & MESH_LF_PSI;
    if (l->has_psi) {
        l->psi[0] = F(MESH_S_PSI_CPU);
        l->psi[1] = F(MESH_S_PSI_MEM);
        l->psi[2] = F(MESH_S_PSI_MEM_FULL);
        l->psi[3] = F(MESH_S_PSI_IO);
        l->psi[4] = F(MESH_S_PSI_IO_FULL);
    }
    l->mem_total_mb = U(MESH_S_MEM_TOTAL_MB);
    l->mem_avail_mb = U(MESH_S_MEM_AVAIL_MB);
    l->mem_free = F(MESH_S_MEM_FREE);
    l->swap_total_mb = U(MESH_S_SWAP_TOTAL_MB);
    l->swap_used_mb = U(MESH_S_SWAP_USED_MB);
    l->swap_out_s = F(MESH_S_SWAP_OUT_S);
    l->compressed_mb = U(MESH_S_MEM_COMPRESSED_MB);
    l->has_mem_level = flags & MESH_LF_MEM_LEVEL;
    l->mem_level = l->has_mem_level ? U(MESH_S_MEM_LEVEL) : 0;
    l->meshd_cpu = F(MESH_S_MESHD_CPU);
    l->meshd_rss_mb = F(MESH_S_MESHD_RSS_MB);
    l->meshd_threads = U(MESH_S_MESHD_THREADS);
    l->disk_free_mb = U(MESH_S_DISK_FREE_MB);
    l->disk_total_mb = U(MESH_S_DISK_TOTAL_MB);
    l->has_thermal = flags & MESH_LF_THERMAL;
    l->thermal = l->has_thermal ? U(MESH_S_THERMAL) : 0;
    l->temp_c = opt(flags & MESH_LF_TEMP, F(MESH_S_TEMP_C));
    l->ncores = n;
    for (size_t i = 0; i < n; i++) {
        l->cores[i][0] = h[MESH_S_CORE_UTIL + i];
        l->cores[i][1] = h[MESH_S_CORE_CLASS + i];
    }
#undef F
#undef U
    return true;
}

bool mt_snapshot_prog_load(const mt_snapshot *s, uint32_t slot, mt_prog_load *p) {
    size_t at = mesh_pload(slot);
    uint32_t flags = mt_snapshot_host_u32(s, at + MESH_PL_FLAGS);
    if (!(flags & MESH_PLF_LOADED)) return false;
#define F(o) mt_snapshot_host_f32(s, at + (o))
    bool g = flags & MESH_PLF_GPU, l = flags & MESH_PLF_LAT;
    p->flags = flags;
    p->share = opt(g, F(MESH_PL_SHARE));
    p->share10 = opt(g, F(MESH_PL_SHARE10));
    p->slice_us = F(MESH_PL_SLICE_US);
    p->gap_us = F(MESH_PL_GAP_US);
    p->max_gap_us = F(MESH_PL_MAX_GAP_US);
    p->observed = F(MESH_PL_OBS);
    p->lat_n = mt_snapshot_host_u32(s, at + MESH_PL_LAT_N);
    p->p50_us = opt(l, F(MESH_PL_P50_US));
    p->p90_us = opt(l, F(MESH_PL_P90_US));
    p->p99_us = opt(l, F(MESH_PL_P99_US));
    p->max_us = opt(l, F(MESH_PL_MAX_US));
    p->task_us = opt(flags & MESH_PLF_TASK, F(MESH_PL_TASK_US));
    p->tasks_s = F(MESH_PL_TASKS_S);
    p->busy = F(MESH_PL_BUSY);
    p->queue_us = F(MESH_PL_QUEUE_US);
#undef F
    return true;
}

bool mt_snapshot_member_load(const mt_snapshot *s, uint32_t k, mt_member_load *m) {
    size_t at = mesh_mload(k);
    uint64_t t = mt_snapshot_host_u64(s, at + MESH_ML_AT_MS);
    if (t == 0) return false;
#define F(o) mt_snapshot_host_f32(s, at + (o))
    uint32_t flags = mt_snapshot_host_u32(s, at + MESH_ML_FLAGS);
    uint32_t cpus = mt_snapshot_host_u32(s, at + MESH_ML_CPUS);
    uint64_t up = mt_snapshot_uptime_ms(s);
    m->age_ms = up > t ? up - t : 0;
    m->flags = flags;
    m->share = opt(flags & MESH_LF_GPU, F(MESH_ML_SHARE));
    m->share10 = opt(flags & MESH_LF_GPU, F(MESH_ML_SHARE10));
    m->max_gap_us = F(MESH_ML_MAX_GAP_US);
    m->cpu = F(MESH_ML_CPU);
    m->cpu_big = F(MESH_ML_CPU_BIG);
    m->cpu_little = F(MESH_ML_CPU_LITTLE);
    m->psi_cpu = opt(flags & MESH_LF_PSI, F(MESH_ML_PSI_CPU));
    m->psi_mem = opt(flags & MESH_LF_PSI, F(MESH_ML_PSI_MEM));
    m->meshd_cpu = F(MESH_ML_MESHD_CPU);
    m->mem_avail_mb = mt_snapshot_host_u32(s, at + MESH_ML_MEM_AVAIL_MB);
    m->mem_total_mb = mt_snapshot_host_u32(s, at + MESH_ML_MEM_TOTAL_MB);
    m->disk_free_mb = mt_snapshot_host_u32(s, at + MESH_ML_DISK_FREE_MB);
    m->cpus_big = cpus & 0xffff;
    m->cpus_little = cpus >> 16;
#undef F
    return true;
}

mt_link_stat mt_snapshot_link(const mt_snapshot *s, uint32_t k) {
    const uint8_t *h = mt_snapshot_host(s) + mesh_link(k);
    size_t n = MESH_LINK_BYTES;
    mt_link_stat l;
    l.system = k;
    l.rx_bytes = mt_get_u64(h, n, MESH_L_RX_BYTES);
    l.tx_bytes = mt_get_u64(h, n, MESH_L_TX_BYTES);
    for (size_t i = 0; i < MESH_NET_KINDS; i++) {
        l.rx_frames[i] = mt_get_u64(h, n, MESH_L_RX_FRAMES + 8 * i);
        l.tx_frames[i] = mt_get_u64(h, n, MESH_L_TX_FRAMES + 8 * i);
    }
    mt_get_str(h, n, MESH_L_LOCAL, 32, l.local);
    mt_get_str(h, n, MESH_L_IFACE, 16, l.iface);
    return l;
}

const size_t MT_NIC_COUNTERS[8] = {MESH_NI_RX_BYTES, MESH_NI_TX_BYTES, MESH_NI_RX_PACKETS, MESH_NI_TX_PACKETS,
                                   MESH_NI_RX_ERRS,  MESH_NI_TX_ERRS,  MESH_NI_RX_DROP,    MESH_NI_TX_DROP};

void mt_snapshot_net_at(const mt_snapshot *s, uint64_t *at_ms, uint32_t *samples) {
    *at_ms = mt_snapshot_host_u64(s, MESH_S_NET_AT_MS);
    *samples = mt_snapshot_host_u32(s, MESH_S_NET_SAMPLES);
}

static mt_nic nic_at(const mt_snapshot *s, size_t at) {
    const uint8_t *h = mt_snapshot_host(s) + at;
    size_t n = MESH_NIC_BYTES;
    mt_nic x;
    mt_get_str(h, n, MESH_NI_NAME, 16, x.name);
    for (int i = 0; i < 8; i++) x.v[i] = mt_get_u64(h, n, MT_NIC_COUNTERS[i]);
    x.links = mt_get_u32(h, n, MESH_NI_LINKS);
    x.count = mt_get_u32(h, n, MESH_NI_COUNT);
    return x;
}

size_t mt_snapshot_nics(const mt_snapshot *s, mt_nic *out) {
    size_t n = mt_snapshot_host_u32(s, MESH_S_NET_NICS);
    if (n > MESH_MAX_NICS) n = MESH_MAX_NICS;
    for (size_t i = 0; i < n; i++) out[i] = nic_at(s, mesh_nic(i));
    return n;
}

mt_nic mt_snapshot_net_total(const mt_snapshot *s) { return nic_at(s, MESH_S_NET_TOTAL); }

void mt_snapshot_ports(const mt_snapshot *s, uint64_t ctl[3], uint64_t http[3]) {
    ctl[0] = mt_snapshot_host_u64(s, MESH_S_CTL_RX);
    ctl[1] = mt_snapshot_host_u64(s, MESH_S_CTL_TX);
    ctl[2] = mt_snapshot_host_u64(s, MESH_S_CTL_REQS);
    http[0] = mt_snapshot_host_u64(s, MESH_S_HTTP_RX);
    http[1] = mt_snapshot_host_u64(s, MESH_S_HTTP_TX);
    http[2] = mt_snapshot_host_u64(s, MESH_S_HTTP_CONNS);
}

void mt_snapshot_lock_pair(const mt_snapshot *s, char name[3 * MESH_NAME_BYTES + 1], uint64_t *step, uint8_t view[32]) {
    const uint8_t *h = mt_snapshot_host(s);
    memcpy(view, h + MESH_S_LOCK_VIEW, 32);
    mt_snapshot_host_str(s, MESH_S_LOCK_NAME, MESH_NAME_BYTES, name);
    *step = mt_get_u64(h, MESH_SNAP_HOST, MESH_S_LOCK_STEP);
}

void mt_cc_label(uint32_t cc, char *out) {
    if (cc == 0) snprintf(out, 32, "-");
    else snprintf(out, 32, "%u.%u", cc / 10, cc % 10);
}

void mt_gpu_label(uint32_t cc, uint32_t backend, char *out) {
    if (mesh_backend_kind(backend) == MESH_BACKEND_METAL) {
        if (mesh_backend_family(backend) > 0) snprintf(out, 32, "apple%u", mesh_backend_family(backend));
        else snprintf(out, 32, "metal");
        return;
    }
    mt_cc_label(cc, out);
}

void mt_blocks_label(uint32_t blocks, uint32_t backend, char *out) {
    if (mesh_backend_kind(backend) == MESH_BACKEND_CUDA) snprintf(out, 32, "%u", blocks);
    else snprintf(out, 32, "-");
}

/* ---- trace */

bool mt_parse_trace(const uint8_t *b, size_t len, mt_trace_prog **out, size_t *n, char **err) {
    size_t per = 8 + (size_t)MESH_TRACE_PER_PROG * MESH_TRACE_BYTES;
    *out = NULL;
    *n = 0;
    if (len % per != 0 || len / per > MESH_PROG_SLOTS) {
        *err = fmt("trace reply is %zu bytes, not a multiple of %zu", len, per);
        return false;
    }
    size_t count = len / per;
    mt_trace_prog *t = calloc(count ? count : 1, sizeof *t);
    if (!t) abort();
    for (size_t c = 0; c < count; c++) {
        const uint8_t *chunk = b + c * per;
        uint32_t slot = mt_get_u32(chunk, per, 0), cnt = mt_get_u32(chunk, per, 4);
        if (slot >= MESH_PROG_SLOTS) {
            *err = fmt("trace slot %u", slot);
            mt_trace_free(t, c);
            return false;
        }
        uint32_t first = cnt > MESH_TRACE_PER_PROG ? cnt - MESH_TRACE_PER_PROG : 0;
        t[c].slot = slot;
        t[c].n = cnt;
        t[c].nrecs = cnt - first;
        t[c].recs = malloc((t[c].nrecs ? t[c].nrecs : 1) * sizeof *t[c].recs);
        if (!t[c].recs) abort();
        for (uint32_t i = first; i < cnt; i++) {
            size_t at = 8 + MESH_TRACE_BYTES * (size_t)(i % MESH_TRACE_PER_PROG);
            uint32_t w0 = mt_get_u32(chunk, per, at);
            t[c].recs[i - first] = (mt_rec){w0 & 0xff, w0 >> 8 & 0xff, w0 >> 16, mt_get_u32(chunk, per, at + 4),
                                            mt_get_u32(chunk, per, at + 8), mt_get_u32(chunk, per, at + 12)};
        }
    }
    *out = t;
    *n = count;
    return true;
}

void mt_trace_free(mt_trace_prog *t, size_t n) {
    if (!t) return;
    for (size_t i = 0; i < n; i++) free(t[i].recs);
    free(t);
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

bool mt_quantile_us(const mt_rec *recs, size_t n, double q, double *out) {
    if (n == 0) return false;
    uint32_t *d = malloc(n * sizeof *d);
    if (!d) abort();
    for (size_t i = 0; i < n; i++) d[i] = mt_rec_ns(&recs[i]);
    qsort(d, n, sizeof *d, cmp_u32);
    size_t i = (size_t)round((double)(n - 1) * q);
    *out = (double)d[i] / 1000.0;
    free(d);
    return true;
}
