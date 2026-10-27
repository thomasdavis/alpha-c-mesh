/* crates/mesh-top/tests/support/mock.rs, ported: the world, its snapshot, trace and commands. */
#include "mesh/top_mock.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const MT_MOCK_CATALOG[MT_MOCK_CATALOG_LEN] = {"doom", "gemm", "hello", "http", "infer", "life", "train"};

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}
static char *xfmt(const char *f, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
static char *xfmt(const char *f, ...) {
    va_list ap, cp;
    va_start(ap, f);
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, f, cp);
    va_end(cp);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, f, ap);
    va_end(ap);
    return s;
}

/* ---- the catalog */

typedef struct {
    uint32_t nodes, view_bytes, snap_bytes, policy, hz, fanout;
    const char *counters[4];
    mt_mock_field fields[8];
    size_t nfields;
    float rate;
    uint32_t task_ns;
} spec;

static mt_mock_prog make(const char *name, uint32_t slot, const spec *s) {
    mt_mock_prog p;
    memset(&p, 0, sizeof p);
    p.slot = slot;
    snprintf(p.name, sizeof p.name, "%s", name);
    p.state = MESH_ST_RUNNING;
    p.nodes = s->nodes;
    p.view_bytes = s->view_bytes;
    p.snap_bytes = s->snap_bytes;
    p.policy = s->policy;
    p.hz = s->hz;
    p.fanout = s->fanout;
    p.peer = MESH_PEER_MODE_NONE;
    memcpy(p.counters, s->counters, sizeof p.counters);
    memcpy(p.fields, s->fields, sizeof p.fields);
    p.nfields = s->nfields;
    p.hash = mesh_fnv1a64((const uint8_t *)name, strlen(name));
    p.regs = 36;
    p.loads = 1;
    p.rate = s->rate;
    p.task_ns = s->task_ns;
    return p;
}

bool mt_mock_catalog_prog(const char *name, uint32_t slot, mt_mock_prog *out) {
    if (strcmp(name, "doom") == 0) {
        spec s = {4, MESH_DOOM_VIEW_BYTES, MESH_DOOM_SNAP_BYTES, MESH_POLICY_PERIODIC, 30, 4, {"frames", "keys", "stalls", "follows"},
                  {{"pos_x", MESH_FT_F32, 0x0}, {"pos_y", MESH_FT_F32, 0x4}, {"dir_x", MESH_FT_F32, 0x8}, {"dir_y", MESH_FT_F32, 0xc},
                   {"frame", MESH_FT_U32, 0x20}, {"keys", MESH_FT_U32, 0x28}}, 6, 120.0f, 300000};
        *out = make(name, slot, &s);
    } else if (strcmp(name, "train") == 0) {
        spec s = {1, 0x1000, 0xd00, MESH_POLICY_CONTINUOUS, 0, 1, {"steps", "timeouts", "inits", ""},
                  {{"a", MESH_FT_F32, 0x0}, {"b", MESH_FT_F32, 0x4}, {"c", MESH_FT_F32, 0x8}, {"d", MESH_FT_F32, 0xc},
                   {"next_step", MESH_FT_U32, 0x1c}, {"err", MESH_FT_U32, 0x20}}, 6, 50.0f, 19000};
        *out = make(name, slot, &s);
        out->peer = MESH_PEER_MODE_LOCKSTEP;
    } else if (strcmp(name, "infer") == 0) {
        spec s = {1, 0x1000, 0x400, MESH_POLICY_PERIODIC, 10, 1, {"runs", "", "", ""},
                  {{"a", MESH_FT_F32, 0x0}, {"b", MESH_FT_F32, 0x4}, {"runs", MESH_FT_U32, 0x10}, {"train_step", MESH_FT_U32, 0x14}}, 4,
                  10.0f, 10000};
        *out = make(name, slot, &s);
    } else if (strcmp(name, "http") == 0) {
        spec s = {1, 0x5000, 0x40, MESH_POLICY_ON_DEMAND, 0, 1, {"", "", "", ""},
                  {{"served", MESH_FT_U32, 0x0}, {"last_code", MESH_FT_U32, 0x4}}, 2, 2.0f, 61000};
        *out = make(name, slot, &s);
    } else if (strcmp(name, "hello") == 0) {
        spec s = {2, 0x1000, 0x40, MESH_POLICY_ON_DEMAND, 0, 1, {"tasks", "inits", "", ""},
                  {{"magic", MESH_FT_U32, 0}, {"last_arg", MESH_FT_U32, 4}, {"last_result", MESH_FT_U32, 8}}, 3, 400.0f, 7000};
        *out = make(name, slot, &s);
    } else if (strcmp(name, "gemm") == 0) {
        spec s = {1, 0x8000, 0x40, MESH_POLICY_PERIODIC, 1, 1, {"gemms", "gflop", "plain", "checks"},
                  {{"gflops", MESH_FT_F32, 0x10}, {"best", MESH_FT_F32, 0x14}}, 2, 1.0f, 8000000};
        *out = make(name, slot, &s);
        out->has_native_pid = true;
        out->native_pid = 40000 + slot;
    } else if (strcmp(name, "life") == 0) {
        spec s = {3, 0x1000, 0x80, MESH_POLICY_PERIODIC, 20, 3, {"generations", "births", "deaths", ""},
                  {{"generation", MESH_FT_U32, 0}, {"alive", MESH_FT_U32, 4}, {"density", MESH_FT_F32, 8}, {"seed", MESH_FT_U64, 0x10},
                   {"rule", MESH_FT_U8, 0x18}}, 5, 60.0f, 300000};
        *out = make(name, slot, &s);
    } else {
        return false;
    }
    return true;
}

/* ---- the world */

void mt_mock_default_assign(uint32_t n, uint32_t fanout, uint8_t out[MESH_PLACE_PARTS_MAX]) {
    memset(out, MESH_PART_NONE, MESH_PLACE_PARTS_MAX);
    if (n < 1) n = 1;
    if (n > fanout) n = fanout;
    uint32_t b = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t c = fanout / n + (k < fanout % n ? 1 : 0);
        for (uint32_t i = 0; i < c; i++) out[b++] = (uint8_t)k;
    }
}

static void log_push(mt_mock_world *w, char *line) {
    w->log = realloc(w->log, (w->nlog + 1) * sizeof *w->log);
    if (!w->log) abort();
    w->log[w->nlog++] = line;
}

static uint64_t elapsed_ms(const mt_mock_world *w) { return mt_since(mt_now(), w->started) / 1000000u; }

static void say(mt_mock_world *w, const char *s) {
    uint64_t ms = elapsed_ms(w);
    log_push(w, xfmt("[%6llu.%03llu] INFO %s", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000), s));
}

void mt_mock_world_init(mt_mock_world *w, const char *host, uint32_t system) {
    memset(w, 0, sizeof *w);
    snprintf(w->host, sizeof w->host, "%s", host);
    w->system = system;
    const char *names[5] = {"doom", "train", "infer", "http", "hello"};
    for (uint32_t i = 0; i < 5; i++) mt_mock_catalog_prog(names[i], i, &w->progs[w->nprogs++]);
    w->epoch = 5;
    w->prog_seq = 5;
    w->pos[0] = MESH_DOOM_START[0];
    w->pos[1] = MESH_DOOM_START[1];
    w->dir[0] = MESH_DOOM_START[2];
    w->dir[1] = MESH_DOOM_START[3];
    w->step0 = mt_now();
    w->started = mt_now();
    w->place[0] = MESH_PLACE_LOCAL;
    memset(w->assign, MESH_PART_NONE, sizeof w->assign);
    w->n = system == MESH_PEER_NONE ? 1 : (system + 1 > 2 ? system + 1 : 2);
    snprintf(w->gpu, sizeof w->gpu, "NVIDIA GB10 (mock)");
    w->cc = 121;
    w->blocks_per_sm = 24;
    snprintf(w->arch, sizeof w->arch, "aarch64");
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) w->waits[k] = 12.0f + 30.0f * (float)k;
    for (int i = 0; i < 5; i++) {
        char *s = xfmt("load %s: jit 1.2 ms, blink 1840 us", names[i]);
        say(w, s);
        free(s);
    }
}

void mt_mock_world_free(mt_mock_world *w) {
    for (size_t i = 0; i < w->nlog; i++) free(w->log[i]);
    free(w->log);
    w->log = NULL;
    w->nlog = 0;
}

static void split_assign(const mt_mock_world *w, uint8_t out[MESH_PLACE_PARTS_MAX]) {
    if (w->assign[0] == MESH_PART_NONE) mt_mock_default_assign(w->n, 4, out);
    else memcpy(out, w->assign, MESH_PLACE_PARTS_MAX);
}

mt_mock_prog *mt_mock_prog_named(mt_mock_world *w, const char *name) {
    for (size_t i = 0; i < w->nprogs; i++)
        if (strcmp(w->progs[i].name, name) == 0) return &w->progs[i];
    return NULL;
}

static bool running(mt_mock_world *w, const char *name) {
    mt_mock_prog *p = mt_mock_prog_named(w, name);
    return p && p->state == MESH_ST_RUNNING;
}

static int64_t ticks(const mt_mock_world *w, const mt_mock_prog *p) {
    return (int64_t)(mt_secs_f64(mt_since(mt_now(), w->started)) * (double)p->rate);
}

static uint32_t done(const mt_mock_world *w, const mt_mock_prog *p) {
    if (p->has_frozen) return p->frozen;
    int64_t d = p->offset + ticks(w, p);
    return (uint32_t)(d > 0 ? d : 0);
}

uint32_t mt_mock_train_step(const mt_mock_world *w) {
    if (w->has_frozen_step) return w->frozen_step;
    float f = mt_secs_f32(mt_since(mt_now(), w->step0)) * 400.0f;
    uint32_t s = f >= 4294967295.0f ? UINT32_MAX : f > 0 ? (uint32_t)f : 0;
    return s / 8 * 8;
}

void mt_mock_params_at(uint32_t step, float out[4]) {
    float k = 1.0f - expf(-(float)step / 3000.0f);
    out[0] = 0.0f;
    out[1] = 2.8f * k;
    out[2] = 0.0f;
    out[3] = -2.0f * k;
}

static uint32_t gt_now(const mt_mock_world *w) { return (uint32_t)mt_since(mt_now(), w->started); }

/* ---- trace */

uint8_t *mt_mock_trace(const mt_mock_world *w, size_t *len) {
    uint32_t now = gt_now(w);
    size_t per = 8 + (size_t)MESH_TRACE_PER_PROG * MESH_TRACE_BYTES;
    uint8_t *out = calloc(w->nprogs ? w->nprogs : 1, per);
    if (!out) abort();
    for (size_t i = 0; i < w->nprogs; i++) {
        const mt_mock_prog *p = &w->progs[i];
        uint8_t *chunk = out + i * per, *ring = chunk + 8;
        uint32_t n = done(w, p);
        float pf = 1e9f / p->rate * (float)p->fanout;
        uint32_t period = pf >= 4294967295.0f ? UINT32_MAX : pf > 0 ? (uint32_t)pf : 0;
        uint32_t newest = !p->has_frozen ? now : now - 2000000000u;
        uint32_t lim = n < MESH_TRACE_PER_PROG ? n : MESH_TRACE_PER_PROG;
        for (uint32_t k = 0; k < lim; k++) {
            uint32_t t = n - 1 - k;
            uint32_t batch = k / p->fanout;
            uint32_t jitter = ((t * 2654435761u) >> 20) % (p->task_ns / 4 + 1);
            uint32_t end = newest - (batch * period + jitter);
            uint32_t dur = p->task_ns - jitter / 2;
            uint32_t node = t % p->nodes;
            size_t at = MESH_TRACE_BYTES * (size_t)(t % MESH_TRACE_PER_PROG);
            mt_put_u32(ring, at, p->slot | node << 8 | MESH_OP_TASK << 16);
            mt_put_u32(ring, at + 4, t);
            mt_put_u32(ring, at + 8, end - dur);
            mt_put_u32(ring, at + 12, end);
        }
        mt_put_u32(chunk, 0, p->slot);
        mt_put_u32(chunk, 4, n);
    }
    *len = w->nprogs * per;
    return out;
}

/* ---- views */

static uint32_t map_at(int32_t x, int32_t y) {
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return 1;
    return mesh_doom_map_cell((size_t)x, (size_t)y);
}

static void doom_frame(const mt_mock_world *w, uint8_t *v) {
    float rows = 48.0f;
    float px = w->pos[0], py = w->pos[1], dx = w->dir[0], dy = w->dir[1];
    float plx = -dy * 0.66f, ply = dx * 0.66f;
    mt_put_f32(v, MESH_DOOM_D_POS_X, px);
    mt_put_f32(v, MESH_DOOM_D_POS_Y, py);
    mt_put_f32(v, MESH_DOOM_D_DIR_X, dx);
    mt_put_f32(v, MESH_DOOM_D_DIR_Y, dy);
    mt_put_f32(v, MESH_DOOM_D_PLANE_X, plx);
    mt_put_f32(v, MESH_DOOM_D_PLANE_Y, ply);
    mt_put_f32(v, MESH_DOOM_D_ROWS, rows);
    mt_put_u32(v, MESH_DOOM_D_MAX_STEPS, 64);
    mt_put_u32(v, MESH_DOOM_D_FRAME, (uint32_t)w->snaps);
    mt_put_u32(v, MESH_DOOM_D_KEYS, w->keys);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) mt_put_u32(v, MESH_DOOM_D_MAP + 4 * (size_t)(16 * y + x), map_at(x, y));
    for (size_t c = 0; c < MESH_DOOM_WIDTH; c++) {
        float cam = 2.0f * (float)c / (float)MESH_DOOM_WIDTH - 1.0f;
        float rx = dx + plx * cam, ry = dy + ply * cam;
        int32_t mx = (int32_t)px, my = (int32_t)py;
        float ddx = fabsf(1.0f / rx), ddy = fabsf(1.0f / ry);
        int32_t sx, sy;
        float sdx, sdy;
        if (rx < 0.0f) {
            sx = -1;
            sdx = (px - (float)mx) * ddx;
        } else {
            sx = 1;
            sdx = ((float)mx + 1.0f - px) * ddx;
        }
        if (ry < 0.0f) {
            sy = -1;
            sdy = (py - (float)my) * ddy;
        } else {
            sy = 1;
            sdy = ((float)my + 1.0f - py) * ddy;
        }
        uint32_t side = 0, kind = 0;
        for (int i = 0; i < 64; i++) {
            if (sdx < sdy) {
                sdx += ddx;
                mx += sx;
                side = 0;
            } else {
                sdy += ddy;
                my += sy;
                side = 1;
            }
            kind = map_at(mx, my);
            if (kind != 0) break;
        }
        float dist = side == 0 ? sdx - ddx : sdy - ddy;
        dist = fmaxf(dist, 1e-3f);
        mt_put_f32(v, MESH_DOOM_D_HEIGHT + 4 * c, rows / dist);
        mt_put_u32(v, MESH_DOOM_D_KIND + 4 * c, kind);
        mt_put_u32(v, MESH_DOOM_D_SIDE + 4 * c, side);
        mt_put_f32(v, MESH_DOOM_D_DIST + 4 * c, dist);
    }
}

static void doom_view(const mt_mock_world *w, uint8_t *v) {
    doom_frame(w, v);
    uint32_t mode = w->place[0], sys = w->place[1];
    if (mode == MESH_PLACE_LOCAL) return;
    size_t show = MESH_DOOM_D_SHOW;
    uint32_t seq = (uint32_t)w->snaps;
    size_t k = seq & 1;
    size_t data = mesh_doom_show_data(seq & 1);
    const uint32_t arrays[4] = {MESH_DOOM_D_HEIGHT, MESH_DOOM_D_KIND, MESH_DOOM_D_SIDE, MESH_DOOM_D_DIST};
    size_t n = 4 * (size_t)MESH_DOOM_WIDTH;
    for (size_t i = 0; i < 4; i++) memmove(v + data + i * n, v + arrays[i], n);
    uint32_t by_part = 0;
    if (mode == MESH_PLACE_SPLIT) {
        uint8_t a[MESH_PLACE_PARTS_MAX];
        split_assign(w, a);
        for (uint32_t b = 0; b < 4; b++) by_part += ((uint32_t)a[b] & 0xf) << (4 * b);
    } else {
        for (uint32_t b = 0; b < 4; b++) by_part += (sys & 0xf) << (4 * b);
    }
    mt_put_u32(v, show + MESH_SH_BUF + MESH_SH_BUF_BYTES * k + MESH_SHB_FRAME, seq);
    mt_put_u32(v, show + MESH_SH_BUF + MESH_SH_BUF_BYTES * k + MESH_SHB_SYS, by_part);
    mt_put_u32(v, show + MESH_SH_MODE, mode);
    mt_put_u32(v, show + MESH_SH_SYS, sys);
    mt_put_u32(v, show + MESH_SH_LAT_US, mode == MESH_PLACE_SPLIT ? 1250 : 0);
    mt_put_u32(v, show + MESH_SH_SEQ, seq);
    if (mode == MESH_PLACE_PIN && sys != w->system)
        for (size_t i = 0; i < 4; i++) memset(v + arrays[i], 0, n);
}

static void train_view(uint32_t step, uint8_t *v) {
    float p[4];
    mt_mock_params_at(step, p);
    for (size_t i = 0; i < 4; i++) mt_put_f32(v, MESH_TRAIN_T_PARAMS + 4 * i, p[i]);
    mt_put_u32(v, MESH_TRAIN_T_NEXT_STEP, step);
    mt_put_u32(v, MESH_TRAIN_T_ROUND, step / 8);
    for (size_t i = 0; i < 256; i++) {
        float x = -1.0f + 2.0f * (float)i / 255.0f;
        mt_put_f32(v, MESH_TRAIN_T_X + 4 * i, x);
        mt_put_f32(v, MESH_TRAIN_T_Y + 4 * i, sinf(3.14159265358979323846f * x));
    }
    for (uint32_t t = step > 256 ? step - 256 : 0; t < step; t++) {
        float loss = 0.5f * expf(-(float)t / 900.0f) + 0.004f + 0.002f * fabsf(sinf((float)t * 0.7f));
        mt_put_f32(v, MESH_TRAIN_T_LOSS + 4 * (size_t)(t % 256), loss);
    }
}

static void infer_view(uint32_t step, uint32_t runs, uint8_t *v) {
    float p[4];
    mt_mock_params_at(step, p);
    for (size_t i = 0; i < 4; i++) mt_put_f32(v, MESH_INFER_I_PARAMS + 4 * i, p[i]);
    mt_put_u32(v, MESH_INFER_I_RUNS, runs);
    mt_put_u32(v, MESH_INFER_I_TRAIN_STEP, step);
    for (size_t i = 0; i < MESH_INFER_POINTS; i++) {
        float x = -1.0f + 2.0f * (float)i / (float)(MESH_INFER_POINTS - 1);
        mt_put_f32(v, MESH_INFER_I_X + 4 * i, x);
        mt_put_f32(v, MESH_INFER_I_Y + 4 * i, p[0] + p[1] * x + p[2] * x * x + p[3] * x * x * x);
    }
}

/* ---- the host block: network and load */

const char *mt_mock_iface(uint32_t a, uint32_t b) {
    uint32_t lo = a < b ? a : b, hi = a > b ? a : b;
    return lo == 0 && hi == 1 ? "enp1s0f0np0" : "enP7s7";
}

static bool is_down(const mt_mock_world *w, uint32_t k) {
    for (size_t i = 0; i < w->ndown; i++)
        if (w->down[i] == k) return true;
    return false;
}

static uint64_t link_at(const mt_mock_world *w, uint32_t k, uint64_t t, uint64_t frames[MESH_NET_KINDS]) {
    memset(frames, 0, MESH_NET_KINDS * sizeof *frames);
    if (w->system == MESH_PEER_NONE || k == w->system || k >= w->n || is_down(w, k)) return 0;
    uint64_t rounds = MT_MOCK_ROUNDS_PER_S * t / 1000, pings = 4 * t / 1000;
    uint64_t place = w->place[0] == MESH_PLACE_SPLIT ? 30 * t / 1000 : 0;
    frames[0] = rounds;
    frames[1] = place;
    frames[2] = 0;
    frames[3] = pings;
    return rounds * MT_MOCK_ROUND_BYTES + pings + place * 2048;
}

typedef struct {
    const char *name;
    uint64_t bytes;
    uint32_t mask;
} nic_sum;

static void net(const mt_mock_world *w, uint64_t t, uint8_t *h) {
    nic_sum ifs[2];
    size_t nifs = 0;
    uint64_t sample = t / 1000 * 1000;
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        uint64_t frames[MESH_NET_KINDS], sf[MESH_NET_KINDS];
        uint64_t bytes = link_at(w, k, t, frames);
        if (bytes == 0) continue;
        size_t at = mesh_link(k);
        mt_put_u64(h, at + MESH_L_RX_BYTES, bytes);
        mt_put_u64(h, at + MESH_L_TX_BYTES, bytes);
        for (size_t i = 0; i < MESH_NET_KINDS; i++) {
            mt_put_u64(h, at + MESH_L_RX_FRAMES + 8 * i, frames[i]);
            mt_put_u64(h, at + MESH_L_TX_FRAMES + 8 * i, frames[i]);
        }
        const char *name = mt_mock_iface(w->system, k);
        char local[64];
        snprintf(local, sizeof local, "192.168.%u.%u:%u", strcmp(name, "enP7s7") == 0 ? 1u : 177u, 11 + w->system, 40000 + k);
        mt_put_str(h, at + MESH_L_LOCAL, 32, local);
        mt_put_str(h, at + MESH_L_IFACE, 16, name);
        size_t e = 0;
        while (e < nifs && strcmp(ifs[e].name, name) != 0) e++;
        if (e == nifs) {
            ifs[nifs].name = name;
            ifs[nifs].bytes = 0;
            ifs[nifs].mask = 0;
            nifs++;
        }
        ifs[e].bytes += link_at(w, k, sample, sf);
        ifs[e].mask |= 1u << k;
    }
    /* BTreeMap order: by name. */
    if (nifs == 2 && strcmp(ifs[0].name, ifs[1].name) > 0) {
        nic_sum t2 = ifs[0];
        ifs[0] = ifs[1];
        ifs[1] = t2;
    }
    if (sample > 0) {
        uint64_t total[8] = {0};
        for (size_t i = 0; i < nifs; i++) {
            uint64_t lan = strcmp(ifs[i].name, "enP7s7") == 0 ? 50 * sample : 0;
            uint64_t b = ifs[i].bytes;
            uint64_t v[8] = {2 * b + lan, 2 * b + lan, b / 70, b / 70, 0, 0, 0, 0};
            size_t at = mesh_nic(i);
            mt_put_str(h, at + MESH_NI_NAME, 16, ifs[i].name);
            for (size_t j = 0; j < 8; j++) {
                mt_put_u64(h, at + MT_NIC_COUNTERS[j], v[j]);
                total[j] += v[j];
            }
            mt_put_u32(h, at + MESH_NI_LINKS, ifs[i].mask);
            mt_put_u32(h, at + MESH_NI_COUNT, 1);
        }
        mt_put_u32(h, MESH_S_NET_NICS, (uint32_t)nifs);
        mt_put_str(h, MESH_S_NET_TOTAL + MESH_NI_NAME, 16, "total");
        for (size_t j = 0; j < 8; j++) mt_put_u64(h, MESH_S_NET_TOTAL + MT_NIC_COUNTERS[j], total[j] + (j < 2 ? 50 * sample : 0));
        mt_put_u32(h, MESH_S_NET_TOTAL + MESH_NI_COUNT, 2);
        mt_put_u64(h, MESH_S_NET_AT_MS, sample);
        mt_put_u32(h, MESH_S_NET_SAMPLES, (uint32_t)(sample / 1000));
    }
    uint64_t reqs = 15 * t / 1000;
    mt_put_u64(h, MESH_S_CTL_RX, 5 * reqs);
    mt_put_u64(h, MESH_S_CTL_TX, 3000 * reqs);
    mt_put_u64(h, MESH_S_CTL_REQS, reqs);
    mt_put_u64(h, MESH_S_HTTP_CONNS, w->http_served);
}

static void load(const mt_mock_world *w, uint64_t t, uint8_t *h) {
    float share = 0.48f + 0.1f * (float)(w->system == MESH_PEER_NONE ? 0 : w->system);
    mt_put_u64(h, MESH_S_LOAD_AT_MS, t > 400 ? (t - 400 > 1 ? t - 400 : 1) : 1);
    mt_put_u32(h, MESH_S_LOAD_SAMPLES, (uint32_t)(t / 1000 + w->snaps));
    mt_put_u32(h, MESH_S_LOAD_FLAGS, MESH_LF_GPU | MESH_LF_PSI | MESH_LF_CLASSES | MESH_LF_TEMP);
    const struct {
        size_t o;
        float v;
    } fs[] = {{MESH_S_GPU_SHARE, share},  {MESH_S_GPU_SHARE10, share},  {MESH_S_GPU_SLICE_US, 2041.0f}, {MESH_S_GPU_GAP_US, 2232.0f},
              {MESH_S_GPU_MAX_GAP_US, 2338.0f}, {MESH_S_GPU_GAPS_S, 704.0f}, {MESH_S_GPU_OBS, 3.0f}, {MESH_S_GPU_INFLATE, 1.0f / share},
              {MESH_S_CPU_UTIL, 0.07f}, {MESH_S_CPU_BIG, 0.113f}, {MESH_S_CPU_LITTLE, 0.022f}, {MESH_S_LOADAVG, 1.27f},
              {MESH_S_PSI_CPU, 0.0016f}, {MESH_S_PSI_MEM, 0.0f}, {MESH_S_PSI_IO, 0.0002f}, {MESH_S_MEM_FREE, 0.709f},
              {MESH_S_MESHD_CPU, 0.012f}, {MESH_S_MESHD_RSS_MB, 161.8f}, {MESH_S_TEMP_C, 68.9f}};
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) mt_put_f32(h, fs[i].o, fs[i].v);
    const struct {
        size_t o;
        uint32_t v;
    } us[] = {{MESH_S_CPUS, 20}, {MESH_S_CPUS_BIG, 10}, {MESH_S_CPUS_LITTLE, 10}, {MESH_S_MEM_TOTAL_MB, 124543},
              {MESH_S_MEM_AVAIL_MB, 88294}, {MESH_S_SWAP_TOTAL_MB, 32767}, {MESH_S_SWAP_USED_MB, 5717},
              {MESH_S_MESHD_THREADS, 7}, {MESH_S_DISK_FREE_MB, 196622}, {MESH_S_DISK_TOTAL_MB, 937290}};
    for (size_t i = 0; i < sizeof us / sizeof us[0]; i++) mt_put_u32(h, us[i].o, us[i].v);
    for (size_t i = 0; i < 20; i++) {
        h[MESH_S_CORE_UTIL + i] = i == 9 ? 100 : (uint8_t)(i * 3);
        h[MESH_S_CORE_CLASS + i] = i % 10 < 5 ? MESH_CORE_LITTLE : MESH_CORE_BIG;
    }
    uint32_t members = w->system == MESH_PEER_NONE ? 1 : w->n;
    for (uint32_t k = 0; k < members; k++) {
        if (is_down(w, k)) continue;
        size_t m = mesh_mload(k);
        mt_put_u64(h, m + MESH_ML_AT_MS, t > 300 ? (t - 300 > 1 ? t - 300 : 1) : 1);
        float ks = 0.48f + 0.1f * (float)k;
        const struct {
            size_t o;
            float v;
        } mf[] = {{MESH_ML_SHARE, ks}, {MESH_ML_SHARE10, ks}, {MESH_ML_MAX_GAP_US, 2338.0f}, {MESH_ML_CPU, 0.07f + 0.01f * (float)k},
                  {MESH_ML_CPU_BIG, 0.11f}, {MESH_ML_CPU_LITTLE, 0.02f}, {MESH_ML_PSI_CPU, 0.0016f}, {MESH_ML_PSI_MEM, 0.0f},
                  {MESH_ML_MESHD_CPU, 0.012f}};
        for (size_t i = 0; i < sizeof mf / sizeof mf[0]; i++) mt_put_f32(h, m + mf[i].o, mf[i].v);
        const struct {
            size_t o;
            uint32_t v;
        } mu[] = {{MESH_ML_MEM_AVAIL_MB, 88294}, {MESH_ML_MEM_TOTAL_MB, 124543}, {MESH_ML_DISK_FREE_MB, 196622},
                  {MESH_ML_CPUS, 10 | 10 << 16}, {MESH_ML_FLAGS, MESH_LF_GPU | MESH_LF_PSI | MESH_LF_CLASSES}};
        for (size_t i = 0; i < sizeof mu / sizeof mu[0]; i++) mt_put_u32(h, m + mu[i].o, mu[i].v);
    }
    for (size_t i = 0; i < w->nprogs; i++) {
        const mt_mock_prog *p = &w->progs[i];
        if (p->state == MESH_ST_EMPTY) continue;
        size_t at = mesh_pload(p->slot);
        uint32_t host = p->has_native_pid && p->state == MESH_ST_RUNNING ? MESH_PLF_HOST : 0;
        mt_put_u32(h, at + MESH_PL_FLAGS, MESH_PLF_LOADED | MESH_PLF_GPU | MESH_PLF_LAT | MESH_PLF_TASK | host);
        if (host) {
            mt_put_f32(h, at + MESH_PL_CPU, 0.975f);
            mt_put_f32(h, at + MESH_PL_CPU10, 0.97f);
            mt_put_u64(h, at + MESH_PL_RSS, (uint64_t)17 << 20);
            mt_put_u32(h, at + MESH_PL_UNITS, 1);
        }
        mt_put_u32(h, at + MESH_PL_LAT_N, 1280);
        const struct {
            size_t o;
            float v;
        } pf[] = {{MESH_PL_SHARE, share}, {MESH_PL_SHARE10, share}, {MESH_PL_SLICE_US, 2042.0f}, {MESH_PL_GAP_US, 2232.0f},
                  {MESH_PL_MAX_GAP_US, 2334.0f}, {MESH_PL_OBS, 1.0f}, {MESH_PL_P50_US, 812.0f}, {MESH_PL_P90_US, 1450.0f},
                  {MESH_PL_P99_US, 2390.0f}, {MESH_PL_MAX_US, 4120.0f}, {MESH_PL_TASK_US, 830.0f}, {MESH_PL_TASKS_S, 120.0f},
                  {MESH_PL_BUSY, 0.1f}};
        for (size_t k = 0; k < sizeof pf / sizeof pf[0]; k++) mt_put_f32(h, at + pf[k].o, pf[k].v);
    }
}

/* ---- synth */

mt_snapshot mt_mock_synth(mt_mock_world *w) {
    w->snaps++;
    float t = mt_secs_f32(mt_since(mt_now(), w->started));
    uint32_t step = mt_mock_train_step(w);
    mt_section *sections = xmalloc(w->nprogs * sizeof *sections);
    float rates[MESH_PROG_SLOTS] = {0};
    uint8_t lock_view[32] = {0};
    float total_rate = 0.0f;
    for (size_t pi = 0; pi < w->nprogs; pi++) {
        mt_mock_prog p = w->progs[pi];
        mt_section s = mt_section_new(p.slot, p.nodes, p.snap_bytes);
        bool run = p.state == MESH_ST_RUNNING;
        uint8_t *e = mt_section_entry(&s);
        mt_put_u32(e, MESH_PT_STATE, p.state);
        mt_put_u32(e, MESH_PT_GEN, 2 * p.loads);
        mt_put_str(e, MESH_PT_NAME, MESH_NAME_BYTES, p.name);
        mt_put_u32(e, MESH_PT_VIEW_OFF, (uint32_t)(MESH_VIEW_HEAP + 0x10000 * (size_t)p.slot));
        mt_put_u32(e, MESH_PT_VIEW_BYTES, p.view_bytes);
        mt_put_u32(e, MESH_PT_SNAP_BYTES, p.snap_bytes);
        mt_put_u32(e, MESH_PT_NODES, p.nodes);
        mt_put_u32(e, MESH_PT_POLICY, p.policy);
        mt_put_u32(e, MESH_PT_HZ, p.hz);
        mt_put_u32(e, MESH_PT_FANOUT, p.fanout);
        mt_put_u32(e, MESH_PT_PEER, p.peer);
        mt_put_u64(e, MESH_PT_PTX_HASH, p.hash);
        mt_put_u32(e, MESH_PT_REGS, p.regs);
        mt_put_u32(e, MESH_PT_CODE_BYTES, 13216);
        mt_put_u32(e, MESH_PT_VIEW_ABI, 1);
        mt_put_u32(e, MESH_PT_RT_ABI, MESH_RT_ABI);
        mt_put_u32(e, MESH_PT_LOADS, p.loads);
        uint32_t d = done(w, &p);
        uint8_t *c = mt_section_ctl(&s);
        mt_put_u32(c, MESH_P_ONLINE, run ? p.nodes : 0);
        mt_put_u32(c, MESH_P_HALTED, run ? 0 : p.nodes);
        mt_put_u32(c, MESH_P_DONE, d);
        mt_put_u32(c, MESH_P_TRACE_N, d);
        mt_put_u32(c, MESH_P_LAUNCHES, p.loads);
        if (p.has_native_pid) {
            mt_put_u32(c, MESH_P_PID, run ? p.native_pid : 0);
            mt_put_u32(c, MESH_P_SPAWNS, 3);
            mt_put_u32(c, MESH_P_CRASHES, 1);
        }
        for (size_t i = 0; i < 4; i++) {
            const char *nm = p.counters[i];
            uint32_t v = 0;
            if (!*nm) v = 0;
            else if (i == 0) v = d / p.fanout;
            else if (strcmp(p.name, "doom") == 0 && i == 1) v = w->keys;
            else if ((strcmp(p.name, "hello") == 0 || strcmp(p.name, "train") == 0) && strcmp(nm, "inits") == 0) v = 1;
            mt_put_u32(c, MESH_P_COUNTERS + 4 * i, v);
        }
        uint8_t *x = mt_section_ext(&s);
        for (size_t i = 0; i < 4; i++) mt_put_str(x, MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES * i, MESH_NAME_BYTES, p.counters[i]);
        mt_put_u32(x, MESH_PX_FIELD_COUNT, (uint32_t)p.nfields);
        if (p.has_native_pid) mt_put_u32(x, MESH_PX_EXEC, MESH_BACKEND_NATIVE);
        if (strcmp(p.name, "doom") == 0) {
            mt_put_u32(x, MESH_PX_PLACE_CAP, MESH_PLACE_CAP_SPLIT);
            mt_put_u32(x, MESH_PX_PLACE, w->place[0]);
            mt_put_u32(x, MESH_PX_PLACE_SYS, w->place[1]);
            uint8_t a[MESH_PLACE_PARTS_MAX];
            if (w->place[0] == MESH_PLACE_SPLIT) split_assign(w, a);
            else memset(a, MESH_PART_NONE, sizeof a);
            memcpy(x + MESH_PX_PLACE_ASSIGN, a, MESH_PLACE_PARTS_MAX);
        }
        for (size_t i = 0; i < p.nfields; i++) {
            size_t at = MESH_PX_FIELDS + MESH_FIELD_BYTES * i;
            mt_put_str(x, at + MESH_FD_NAME, MESH_FD_NAME_MAX + 1, p.fields[i].name);
            mt_put_u32(x, at + MESH_FD_TYPE, p.fields[i].type);
            mt_put_u32(x, at + MESH_FD_OFF, (uint32_t)p.fields[i].off);
        }
        for (size_t n = 0; n < p.nodes; n++) {
            uint8_t *r = mt_section_node_rec(&s, n);
            mt_put_u32(r, MESH_N_SM, (uint32_t)((p.slot * 7 + n * 5) % 48));
            bool frozen = !run;
            for (size_t k = 0; k < w->nstalled; k++) frozen |= strcmp(w->stalled[k].name, p.name) == 0 && w->stalled[k].node == n;
            mt_put_u32(r, MESH_N_HEARTBEAT, frozen ? 1000 : (uint32_t)w->snaps * 13 + (uint32_t)n);
            mt_put_u32(r, MESH_N_TASKS, d / p.nodes);
            mt_put_u32(r, MESH_N_LAST_OP, MESH_OP_TASK);
            float dutyf = p.rate * (float)p.task_ns / 1e9f / (float)p.nodes;
            dutyf = dutyf < 0.05f ? 0.05f : dutyf > 0.95f ? 0.95f : dutyf;
            double duty = (double)dutyf;
            double ph = (double)n * 0.37 + (double)t * 0.8;
            double tt = run ? (double)t : 0.0;
            double busy = 1e9 * (1.0 + duty * tt - 0.04 * cos(ph));
            mt_put_u64(r, MESH_N_BUSY_NS, busy > 0 ? (busy >= 1.8446744073709552e19 ? UINT64_MAX : (uint64_t)busy) : 0);
        }
        uint8_t *v = mt_section_view(&s);
        if (strcmp(p.name, "doom") == 0) doom_view(w, v);
        else if (strcmp(p.name, "train") == 0) {
            train_view(step, v);
            memcpy(lock_view, v, 32);
        } else if (strcmp(p.name, "infer") == 0) infer_view(step, d, v);
        else if (strcmp(p.name, "http") == 0) {
            mt_put_u32(v, MESH_HTTP_HV_SERVED, w->http_served);
            mt_put_u32(v, MESH_HTTP_HV_LAST_CODE, 200);
        } else if (strcmp(p.name, "hello") == 0) {
            memcpy(v, "HELO", 4);
            mt_put_u32(v, 4, w->hello_last[0]);
            mt_put_u32(v, 8, w->hello_last[1]);
        } else if (strcmp(p.name, "life") == 0) {
            mt_put_u32(v, 0, d);
            mt_put_u32(v, 4, 37 + d % 11);
            mt_put_f32(v, 8, 0.29f);
            mt_put_u64(v, 0x10, 0x5eed1234abcdu);
            v[0x18] = 23;
            for (size_t i = 0x20; i < mt_section_view_len(&s); i++) v[i] = ((uint32_t)i * 7 + d) % 5 == 0 ? '#' : '.';
        }
        if (run) {
            rates[p.slot] = p.rate + (strcmp(p.name, "train") == 0 ? 0.0f : 0.1f * p.rate * sinf(t * 0.9f));
            total_rate += rates[p.slot];
        }
        sections[pi] = s;
    }
    static uint8_t head[MESH_H_BYTES];
    memset(head, 0, sizeof head);
    mt_put_u32(head, MESH_H_MAGIC, MESH_MAGIC);
    mt_put_u32(head, MESH_H_VERSION, MESH_VERSION);
    mt_put_u32(head, MESH_H_PEER, w->system);
    mt_put_u32(head, MESH_H_EPOCH, w->epoch);
    mt_put_u32(head, MESH_H_PROG_SEQ, w->prog_seq);
    mt_put_u32(head, MESH_H_SMS, 48);
    mt_put_u32(head, MESH_H_SYSTEMS, w->n);
    mt_put_u32(head, MESH_H_CC, w->cc);
    mt_put_u32(head, MESH_H_BLOCKS_PER_SM, w->blocks_per_sm);
    mt_put_u32(head, MESH_H_SMS_TOTAL, 48);
    uint8_t *h = calloc(1, MESH_SNAP_HOST);
    if (!h) abort();
    uint64_t uptime = w->has_clock_ms ? w->clock_ms : elapsed_ms(w);
    mt_put_u64(h, MESH_S_UPTIME_MS, uptime);
    mt_put_u32(h, MESH_S_EPOCH, w->epoch);
    mt_put_u32(h, MESH_S_SYSTEM, w->system);
    mt_put_u32(h, MESH_S_PEER_STATE, w->system == MESH_PEER_NONE ? 0 : 2);
    mt_put_u32(h, MESH_S_PROGRAMS, (uint32_t)w->nprogs);
    mt_put_u32(h, MESH_S_SMS_TOTAL, 48);
    mt_put_u32(h, MESH_S_GREEN, 1);
    mt_put_f32(h, MESH_S_TASKS_PER_S, total_rate);
    uint32_t used = 0;
    for (size_t i = 0; i < w->nprogs; i++) used += (w->progs[i].view_bytes + (uint32_t)MESH_VIEW_PAGE - 1) / (uint32_t)MESH_VIEW_PAGE;
    mt_put_u32(h, MESH_S_HEAP_FREE, (uint32_t)MESH_VIEW_PAGES - used);
    mt_put_u32(h, MESH_S_SMS, 48);
    mt_put_f32(h, MESH_S_PEER_RTT_US, 38.5f);
    uint32_t natives = 0;
    for (size_t i = 0; i < w->nprogs; i++) natives += w->progs[i].has_native_pid && w->progs[i].state == MESH_ST_RUNNING;
    mt_put_u32(h, MESH_S_WORKERS, natives);
    mt_put_f32(h, MESH_S_EXEC_CPU, 0.975f * (float)natives);
    mt_put_u64(h, MESH_S_EXEC_RSS, ((uint64_t)17 << 20) * natives);
    mt_put_u32(h, MESH_S_CPUS, 20);
    if (mt_mock_prog_named(w, "train")) {
        mt_put_u64(h, MESH_S_LOCK_STEP, step);
        memcpy(h + MESH_S_LOCK_VIEW, lock_view, 32);
        mt_put_str(h, MESH_S_LOCK_NAME, MESH_NAME_BYTES, "train");
    }
    mt_put_u32(h, MESH_S_HTTP_REQUESTS, w->http_served);
    mt_put_u32(h, MESH_S_BLINK_US, 1840);
    mt_put_str(h, MESH_S_HOSTNAME, 32, w->host);
    mt_put_str(h, MESH_S_GPU, 32, w->gpu);
    mt_put_str(h, MESH_S_PROGRAMS_DIR, 64, "out/programs");
    mt_put_str(h, MESH_S_LAST_HTTP, 64, w->last_http);
    char addr[32];
    snprintf(addr, sizeof addr, "0.0.0.0:%u", (unsigned)MESH_HTTP_PORT);
    mt_put_str(h, MESH_S_HTTP_ADDR, 64, addr);
    for (size_t i = 0; i < MESH_PROG_SLOTS; i++) mt_put_f32(h, MESH_S_RATES + 4 * i, rates[i]);
    mt_put_u64(h, MESH_S_LAST_BLINK_MS, 1200);
    mt_put_u32(h, MESH_S_SYSTEMS, w->n);
    mt_put_str(h, MESH_S_ARCH, 16, w->arch);
    if (w->system != MESH_PEER_NONE) {
        uint32_t linked = 0;
        for (uint32_t k = 0; k < w->n; k++) {
            size_t m = mesh_member(k);
            bool up = !is_down(w, k);
            if (up) linked |= 1u << k;
            mt_put_u32(h, m + MESH_M_STATE, up ? MESH_MS_UP : MESH_MS_ABSENT);
            bool me = k == w->system;
            mt_put_u32(h, m + MESH_M_CC, me ? w->cc : 121);
            mt_put_u32(h, m + MESH_M_SMS, 48);
            mt_put_u32(h, m + MESH_M_SMS_TOTAL, 48);
            mt_put_u32(h, m + MESH_M_BLOCKS_PER_SM, me ? w->blocks_per_sm : 24);
            mt_put_f32(h, m + MESH_M_RTT_US, me ? 0.0f : 38.5f);
            mt_put_f32(h, m + MESH_M_WAIT_US, me ? 0.0f : w->waits[k]);
            mt_put_u32(h, m + MESH_M_ROUNDS, me ? 0 : 400);
            mt_put_u64(h, m + MESH_M_ROUND, step);
            mt_put_str(h, m + MESH_M_GPU, 32, me ? w->gpu : "NVIDIA GB10 (mock)");
            mt_put_str(h, m + MESH_M_ARCH, 16, me ? w->arch : "aarch64");
            char host[64];
            if (me) snprintf(host, sizeof host, "%s", w->host);
            else snprintf(host, sizeof host, "mock-%u", k);
            mt_put_str(h, m + MESH_M_HOST, 32, host);
        }
        mt_put_u32(h, MESH_S_LINKED, linked);
    }
    net(w, uptime, h);
    load(w, uptime, h);
    mt_snapshot snap = mt_snapshot_assemble(head, sections, w->nprogs, h);
    for (size_t i = 0; i < w->nprogs; i++) mt_section_free(&sections[i]);
    free(sections);
    free(h);
    return snap;
}

/* ---- commands */

static const char *dir_name(const char *arg, char *buf, size_t cap) {
    snprintf(buf, cap, "%s", arg);
    size_t l = strlen(buf);
    while (l && buf[l - 1] == '/') buf[--l] = 0;
    const char *suf = "/program.toml";
    size_t sl = strlen(suf);
    while (l >= sl && strcmp(buf + l - sl, suf) == 0) {
        l -= sl;
        buf[l] = 0;
    }
    const char *slash = strrchr(buf, '/');
    return slash ? slash + 1 : buf;
}

static char *doom_key(mt_mock_world *w, const char *k) {
    float dx = w->dir[0], dy = w->dir[1];
    float a = 0.0f;
    float vx = 0, vy = 0;
    bool walk = false;
    if (strcmp(k, "w") == 0) walk = true, vx = dx, vy = dy;
    else if (strcmp(k, "s") == 0) walk = true, vx = -dx, vy = -dy;
    else if (strcmp(k, "q") == 0) walk = true, vx = dy, vy = -dx;
    else if (strcmp(k, "e") == 0) walk = true, vx = -dy, vy = dx;
    else if (strcmp(k, "a") == 0) a = -atan2f(MESH_DOOM_TURN_SIN, MESH_DOOM_TURN_COS);
    else if (strcmp(k, "d") == 0) a = atan2f(MESH_DOOM_TURN_SIN, MESH_DOOM_TURN_COS);
    else return xfmt("err unknown key %s", k);
    if (walk) {
        float nx = w->pos[0] + vx * MESH_DOOM_MOVE, ny = w->pos[1] + vy * MESH_DOOM_MOVE;
        if (map_at((int32_t)nx, (int32_t)ny) == 0) {
            w->pos[0] = nx;
            w->pos[1] = ny;
        }
    } else {
        float s = sinf(a), c = cosf(a);
        w->dir[0] = dx * c - dy * s;
        w->dir[1] = dx * s + dy * c;
    }
    return xfmt("ok");
}

static char *command(mt_mock_world *w, const char *cmd, const char *arg) {
    char name[64];
    {
        const char *p = arg;
        while (*p == ' ') p++;
        size_t l = 0;
        while (p[l] && p[l] != ' ' && l < sizeof name - 1) l++;
        memcpy(name, p, l);
        name[l] = 0;
    }
    char *r = NULL;
    if (strcmp(cmd, "place") == 0 && strcmp(name, "doom") == 0) {
        char words[3][32] = {"", "", ""};
        int nw = sscanf(arg, "%*s %31s %31s %31s", words[0], words[1], words[2]);
        uint64_t k = 0;
        if (nw == 1 && strcmp(words[0], "local") == 0) {
            w->place[0] = MESH_PLACE_LOCAL;
            w->place[1] = 0;
            r = xfmt("ok local");
        } else if (nw == 1 && strcmp(words[0], "split") == 0) {
            mt_mock_default_assign(w->n, 4, w->assign);
            w->place[0] = MESH_PLACE_SPLIT;
            w->place[1] = 0;
            r = xfmt("ok split");
        } else if (nw == 2 && strcmp(words[0], "pin") == 0 && mt_parse_u64(words[1], &k) && k < w->n) {
            w->place[0] = MESH_PLACE_PIN;
            w->place[1] = (uint32_t)k;
            r = xfmt("ok pin %s", words[1]);
        } else {
            r = xfmt("err usage: place <name> [local | pin <k> | split]");
        }
    } else if (strcmp(cmd, "start") == 0 || strcmp(cmd, "stop") == 0) {
        bool start = strcmp(cmd, "start") == 0;
        if (strcmp(name, "train") == 0) {
            if (start) {
                if (w->has_frozen_step) {
                    uint32_t f = w->frozen_step;
                    w->has_frozen_step = false;
                    w->step0 = mt_now() - mt_from_secs_f32((float)f / 400.0f);
                }
            } else if (!w->has_frozen_step) {
                w->frozen_step = mt_mock_train_step(w);
                w->has_frozen_step = true;
            }
        }
        mt_mock_prog *p = mt_mock_prog_named(w, name);
        if (p) {
            uint32_t d = done(w, p);
            int64_t t = ticks(w, p);
            if (start) {
                if (p->has_frozen) {
                    p->offset = (int64_t)p->frozen - t;
                    p->has_frozen = false;
                }
            } else {
                p->has_frozen = true;
                p->frozen = d;
            }
            p->state = start ? MESH_ST_RUNNING : MESH_ST_LOADED;
            r = xfmt("ok");
        } else {
            r = xfmt("err %s is not loaded", name);
        }
    } else if (strcmp(cmd, "load") == 0) {
        char buf[256];
        const char *n = dir_name(arg, buf, sizeof buf);
        bool has_slot = false;
        uint32_t slot = 0;
        for (uint32_t s = 0; s < MESH_PROG_SLOTS && !has_slot; s++) {
            bool used = false;
            for (size_t i = 0; i < w->nprogs; i++) used |= w->progs[i].slot == s;
            if (!used) {
                has_slot = true;
                slot = s;
            }
        }
        mt_mock_prog p;
        if (mt_mock_prog_named(w, n)) r = xfmt("err name exists");
        else if (has_slot && mt_mock_catalog_prog(n, slot, &p)) {
            w->progs[w->nprogs++] = p;
            for (size_t i = 1; i < w->nprogs; i++)
                for (size_t j = i; j > 0 && w->progs[j - 1].slot > w->progs[j].slot; j--) {
                    mt_mock_prog t = w->progs[j];
                    w->progs[j] = w->progs[j - 1];
                    w->progs[j - 1] = t;
                }
            w->epoch++;
            r = xfmt("ok slot %u blink 1840", slot);
        } else if (!has_slot) r = xfmt("err no slot");
        else r = xfmt("err manifest: no program.toml in %s", arg);
    } else if (strcmp(cmd, "upgrade") == 0) {
        mt_mock_prog *p = mt_mock_prog_named(w, name);
        if (p) {
            p->has_retained = true;
            p->retained = p->hash;
            p->loads++;
            char hn[64];
            snprintf(hn, sizeof hn, "%s%u", p->name, p->loads);
            p->hash = mesh_fnv1a64((const uint8_t *)hn, strlen(hn));
            w->epoch++;
            r = xfmt("ok blink 2100 view kept");
        } else r = xfmt("err %s is not loaded", name);
    } else if (strcmp(cmd, "rollback") == 0) {
        mt_mock_prog *p = mt_mock_prog_named(w, name);
        if (!p) r = xfmt("err %s is not loaded", name);
        else if (!p->has_retained) r = xfmt("err %s has no retained image", name);
        else {
            uint64_t h = p->retained;
            p->retained = p->hash;
            p->hash = h;
            p->loads++;
            w->epoch++;
            r = xfmt("ok blink 900");
        }
    } else if (strcmp(cmd, "unload") == 0) {
        size_t i = 0;
        while (i < w->nprogs && strcmp(w->progs[i].name, name) != 0) i++;
        if (i < w->nprogs) {
            memmove(&w->progs[i], &w->progs[i + 1], (w->nprogs - i - 1) * sizeof *w->progs);
            w->nprogs--;
            r = xfmt("ok");
        } else r = xfmt("err %s is not loaded", name);
    } else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "train-reset") == 0) {
        const char *n = strcmp(cmd, "train-reset") == 0 ? "train" : name;
        mt_mock_prog *p = mt_mock_prog_named(w, n);
        if (p) {
            p->offset = -ticks(w, p);
            if (p->has_frozen) p->frozen = 0;
            if (strcmp(n, "train") == 0) {
                w->step0 = mt_now();
                if (w->has_frozen_step) w->frozen_step = 0;
            }
            r = xfmt("ok");
        } else r = xfmt("err %s is not loaded", n);
    } else if (strcmp(cmd, "task") == 0) {
        uint64_t a[8];
        size_t na = 0;
        char copy[256];
        snprintf(copy, sizeof copy, "%s", arg);
        char *save = NULL, *tok = strtok_r(copy, " ", &save);
        for (tok = tok ? strtok_r(NULL, " ", &save) : NULL; tok && na < 8; tok = strtok_r(NULL, " ", &save)) {
            uint64_t v;
            if (mt_parse_u64(tok, &v) && v <= UINT32_MAX) a[na++] = v;
        }
        if (strcmp(name, "hello") == 0 && na == 2 && a[0] == 0) {
            uint32_t x = (uint32_t)a[1];
            w->hello_last[0] = x;
            w->hello_last[1] = x * 3 + 7;
            r = xfmt("ok %u", x * 3 + 7);
        } else if (strcmp(name, "hello") == 0 && na == 2 && a[0] == 1) r = xfmt("ok 0");
        else if (strcmp(name, "hello") == 0 && na == 2) r = xfmt("ok %u", (unsigned)MESH_R_BAD_OP);
        else r = xfmt("err usage: task <name> <op> <arg>");
    } else if (strcmp(cmd, "input") == 0 || strcmp(cmd, "key") == 0) {
        char prog[64] = "", k[64] = "";
        if (strcmp(cmd, "key") == 0) {
            snprintf(prog, sizeof prog, "doom");
            snprintf(k, sizeof k, "%s", arg);
        } else {
            const char *sp = strchr(arg, ' ');
            if (sp) {
                snprintf(prog, sizeof prog, "%.*s", (int)(sp - arg), arg);
                snprintf(k, sizeof k, "%s", sp + 1);
            } else {
                snprintf(prog, sizeof prog, "%s", arg);
            }
        }
        if (strcmp(prog, "doom") != 0 || !mt_mock_prog_named(w, prog)) return xfmt("err %s has no input queue", prog);
        w->keys++;
        r = doom_key(w, k);
    } else {
        return xfmt("err unknown command %s", cmd);
    }
    if (strncmp(r, "ok", 2) == 0 && strcmp(cmd, "task") != 0 && strcmp(cmd, "input") != 0 && strcmp(cmd, "key") != 0) {
        w->prog_seq++;
        char *s = xfmt("%s %s: %s", cmd, arg, r);
        say(w, s);
        free(s);
    } else if (strncmp(r, "err", 3) == 0) {
        uint64_t ms = elapsed_ms(w);
        log_push(w, xfmt("[%6llu.%03llu] ERROR %s %s: %s", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000), cmd, arg, r));
    }
    return r;
}

static uint8_t *http(mt_mock_world *w, const char *path, size_t *len) {
    w->http_served++;
    snprintf(w->last_http, sizeof w->last_http, "GET %s HTTP/1.0", path);
    size_t cap = 8192 + strlen(path) * 2;
    char *body = xmalloc(cap);
    size_t n = (size_t)snprintf(body, cap, "mock http for %s on %s\n", path, w->host);
    for (int i = 0; i < 60; i++) n += (size_t)snprintf(body + n, cap - n, "line %02d: the GPU would compute this\n", i);
    char *resp = xfmt("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n\r\n%s", n, body);
    free(body);
    *len = strlen(resp);
    return (uint8_t *)resp;
}

void mt_mock_request(mt_mock_world *w, const char *line, uint8_t **bytes, size_t *len, char **text) {
    *bytes = NULL;
    *text = NULL;
    *len = 0;
    /* trim */
    const char *s = line;
    size_t l = strlen(s);
    while (l && (*s == ' ' || (*s >= '\t' && *s <= '\r'))) s++, l--;
    while (l && (s[l - 1] == ' ' || (s[l - 1] >= '\t' && s[l - 1] <= '\r'))) l--;
    char *req = xmalloc(l + 1);
    memcpy(req, s, l);
    req[l] = 0;
    char *sp = strchr(req, ' ');
    const char *arg = "";
    if (sp) {
        *sp = 0;
        arg = sp + 1;
    }
    const char *cmd = req;
    if (strcmp(cmd, "snap") == 0) {
        mt_snapshot snap = mt_mock_synth(w);
        *bytes = snap.bytes;
        *len = snap.len;
        snap.bytes = NULL;
        for (size_t i = 0; i < snap.nsections; i++) free(snap.sections[i].bytes);
        free(snap.sections);
    } else if (strcmp(cmd, "trace") == 0) {
        *bytes = mt_mock_trace(w, len);
    } else if (strcmp(cmd, "programs") == 0) {
        size_t cap = 8192, n = 0;
        char *o = xmalloc(cap);
        o[0] = 0;
        for (size_t i = 0; i < w->nprogs; i++) {
            const mt_mock_prog *p = &w->progs[i];
            char rate[32];
            snprintf(rate, sizeof rate, "%.1f", (double)(p->state == MESH_ST_RUNNING ? p->rate : 0.0f));
            n += (size_t)snprintf(o + n, cap - n, "%u %s %s %u %u %s %s %016llx %u 0x%zx+0x%x\n", p->slot, p->name, MESH_STATES[p->state],
                                  p->nodes, 0u, MESH_POLICIES[p->policy], rate, (unsigned long long)p->hash, p->regs,
                                  (size_t)MESH_VIEW_HEAP + 0x10000 * (size_t)p->slot, p->view_bytes);
            n += (size_t)snprintf(o + n, cap - n, "fields %s ", p->name);
            for (size_t f = 0; f < p->nfields; f++) n += (size_t)snprintf(o + n, cap - n, "%s%s", f ? " " : "", p->fields[f].name);
            n += (size_t)snprintf(o + n, cap - n, "\ncounters %s %s %s %s %s\n", p->name, p->counters[0], p->counters[1], p->counters[2], p->counters[3]);
        }
        *bytes = (uint8_t *)o;
        *len = n;
    } else if (strcmp(cmd, "catalog") == 0) {
        char *o = xmalloc(256);
        size_t n = 0;
        for (int i = 0; i < MT_MOCK_CATALOG_LEN; i++) n += (size_t)snprintf(o + n, 256 - n, "%s\n", MT_MOCK_CATALOG[i]);
        *bytes = (uint8_t *)o;
        *len = n;
    } else if (strcmp(cmd, "log") == 0) {
        uint64_t n = 50;
        if (!mt_parse_u64(arg, &n)) n = 50;
        size_t from = w->nlog > n ? w->nlog - n : 0, total = 0;
        for (size_t i = from; i < w->nlog; i++) total += strlen(w->log[i]) + 1;
        char *o = xmalloc(total + 1);
        size_t k = 0;
        for (size_t i = from; i < w->nlog; i++) k += (size_t)sprintf(o + k, "%s\n", w->log[i]);
        *bytes = (uint8_t *)o;
        *len = k;
    } else if (strcmp(cmd, "peek") == 0) {
        char name[64];
        uint64_t off, plen;
        char offs[32], lens[32];
        if (sscanf(arg, "%63s %31s %31s", name, offs, lens) != 3 || !mt_parse_u64(offs, &off) || !mt_parse_u64(lens, &plen)) {
            *text = xfmt("err usage: peek <name> <off> <len>");
        } else {
            mt_snapshot snap = mt_mock_synth(w);
            const mt_section *p = mt_snapshot_by_name(&snap, name);
            if (!p) *text = xfmt("err %s is not loaded", name);
            else if (off + plen > mt_section_view_len(p)) *text = xfmt("err peek out of the snap bytes");
            else {
                *bytes = xmalloc((size_t)plen);
                memcpy(*bytes, mt_section_view(p) + off, (size_t)plen);
                *len = (size_t)plen;
            }
            mt_snapshot_free(&snap);
        }
    } else if (strcmp(cmd, "http") == 0) {
        if (running(w, "http")) *bytes = http(w, arg, len);
        else *text = xfmt("err http is not running");
    } else {
        *text = command(w, cmd, arg);
    }
    free(req);
}
