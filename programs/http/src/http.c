/* http: an HTTP/1.0 GET server computed on the GPU. meshd only moves bytes: it
 * writes the request into the request window and publishes OP_REQ; the warp
 * parses, routes and renders the whole response (status line, headers, body).
 * Pages are generic over the program table (`reads = ["*"]`):
 *   /  /programs  /status          every loaded program, from the table
 *   /start/<name>  /stop/<name>    the slot's GPU-owned P_RUN_REQ (meshd honours it)
 *   /view/<name>                   the program's declared `fields`
 *   /load/<name>                   the load request words (view http), meshd honours it
 *   /doom  /train                  those programs' pages, found by name
 *   /render                        pathtrace's progress (pathtrace, else pathtrace-live)
 *   /render.bmp  /render.ppm       its current image (24-bit BMP; binary PPM),
 *                                  copied from its tile-major view by the whole warp
 * Errors: 400, 404, 405 (anything but GET), 409, 503. Layout: view http.
 * The warp gathers the request (and, for /doom, the per-column wall spans)
 * with coalesced loads + shuffles; lane 0 then does the serial string work in
 * one pass: Content-Length is a fixed-width field, filled in after the body.
 * v1's renderer, kept as written (`v != v` is the NaN test).
 * Ported from alpha-rust-mesh programs/http/src/lib.rs. */
#include "mesh/rt.h"
#include "pathtrace/view.h"

/* Bounded byte writer into the response window. */
typedef struct {
    uint8_t *p;
    uint32_t n, cap;
} http_w;

static inline __attribute__((always_inline)) void http_b(http_w *w, uint8_t c) {
    if (w->n < w->cap) { mesh_rt_wrb(w->p + w->n, c); w->n++; }
}
__attribute__((noinline)) static void http_s(http_w *w, const char *s) {
    for (; *s; s++) http_b(w, (uint8_t)*s);
}
/* A NUL-padded name. */
__attribute__((noinline)) static void http_name(http_w *w, const uint8_t n[MESH_NAME_BYTES]) {
    for (uint32_t i = 0; i < MESH_NAME_BYTES && n[i]; i++) http_b(w, n[i]);
}
__attribute__((noinline)) static void http_u(http_w *w, uint64_t v) {
    uint8_t d[20];
    uint32_t k = 0;
    for (;;) {
        d[k] = (uint8_t)('0' + v % 10);
        v /= 10;
        k++;
        if (v == 0 || k == 20) break;
    }
    while (k > 0) http_b(w, d[--k]);
}
static void http_i(http_w *w, int32_t v) {
    if (v < 0) http_b(w, '-');
    http_u(w, v < 0 ? (uint64_t)(-(int64_t)v) : (uint64_t)v);
}
static void http_fixed(http_w *w, double x, uint32_t dec) {
    uint64_t scale = 1;
    for (uint32_t i = 0; i < dec; i++) scale *= 10;
    uint64_t ip = (uint64_t)x;
    uint64_t fp = (uint64_t)((x - (double)ip) * (double)scale + 0.5);
    if (fp >= scale) { ip += 1; fp -= scale; }
    http_u(w, ip);
    if (dec > 0) {
        http_b(w, '.');
        for (uint64_t div = scale / 10; div > 0; div /= 10) http_b(w, (uint8_t)('0' + fp / div % 10));
    }
}
/* Fixed point with dec decimals for ordinary magnitudes, d.dddde[-]N
 * otherwise; handles sign, NaN and infinities. */
__attribute__((noinline)) static void http_f(http_w *w, float v, uint32_t dec) {
    if (v != v) { http_s(w, "nan"); return; }
    double x = (double)v;
    if (x < 0.0) { http_b(w, '-'); x = -x; }
    if (x == __builtin_inf()) { http_s(w, "inf"); return; }
    if (x != 0.0 && (x < 1e-3 || x >= 1e9)) {
        int32_t e = 0;
        while (x >= 10.0) { x /= 10.0; e++; }
        while (x < 1.0) { x *= 10.0; e--; }
        http_fixed(w, x, 4);
        http_b(w, 'e');
        http_i(w, e);
        return;
    }
    http_fixed(w, x, dec);
}

/* Route codes (lane 0 decides, the warp learns them by shuffle). */
enum {
    HTTP_RT_INDEX, HTTP_RT_PROGRAMS, HTTP_RT_STATUS, HTTP_RT_DOOM, HTTP_RT_TRAIN, HTTP_RT_VIEW, HTTP_RT_OK, HTTP_RT_LOAD,
    HTTP_RT_BAD, HTTP_RT_NOT_FOUND, HTTP_RT_NO_METHOD, HTTP_RT_BUSY, HTTP_RT_CONFLICT, HTTP_RT_RENDER, HTTP_RT_PPM, HTTP_RT_BMP,
};

/* The program table, read through the mailbox (READS_ALL). */
typedef struct {
    uint8_t *mb;
} http_tab;
static uint32_t tab_ent(http_tab t, uint32_t s, size_t off) { return mesh_rt_ld_relaxed(t.mb + mesh_prog_entry(s) + off); }
static uint32_t tab_ctl(http_tab t, uint32_t s, size_t off) { return mesh_rt_ld_relaxed(t.mb + mesh_prog_ctl(s) + off); }
static uint32_t tab_hdr(http_tab t, size_t off) { return mesh_rt_ld_relaxed(t.mb + off); }
/* A word of the load page (the host block's S_LOAD.. regions, mirrored by meshd). */
static uint32_t tab_load(http_tab t, size_t s_off) { return mesh_rt_ld_relaxed(t.mb + mesh_load_page(s_off)); }
static float tab_loadf(http_tab t, size_t s_off) { return mesh_f32_from_bits(tab_load(t, s_off)); }
static uint8_t *tab_ext(http_tab t, uint32_t s, size_t off) { return t.mb + mesh_prog_ext(s) + off; }
static bool tab_used(http_tab t, uint32_t s) { return tab_ent(t, s, MESH_PT_STATE) != MESH_ST_EMPTY; }
static void http_bytes16(const uint8_t *p, uint8_t n[16]) {
    for (int i = 0; i < 16; i++) n[i] = *(const volatile uint8_t *)(p + i);
}
static void tab_name(http_tab t, uint32_t s, uint8_t n[16]) { http_bytes16(t.mb + mesh_prog_entry(s) + MESH_PT_NAME, n); }

static inline __attribute__((always_inline)) uint8_t http_byte(const uint32_t req[64], size_t i) {
    return i >= MESH_HTTP_REQ_MAX ? 0 : (uint8_t)(req[i >> 2] >> (8 * (i & 3)));
}
/* req[at..end] == s */
static bool http_eq(const uint32_t req[64], size_t at, size_t end, const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    if (end < at || end - at != n) return false;
    for (size_t i = 0; i < n; i++)
        if (http_byte(req, at + i) != (uint8_t)s[i]) return false;
    return true;
}
/* req[at..] starts with s */
static bool http_starts(const uint32_t req[64], size_t at, size_t end, const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return end >= at + n && http_eq(req, at, at + n, s);
}

static void http_init(const mesh_ctx *ctx) {
    if (mesh_ctx_leader(ctx)) {
        /* A pending load request from before a reset is dropped: ack == seq. */
        mesh_view v = mesh_ctx_view(ctx);
        mesh_view_set_u32(v, MESH_HTTP_HV_LOAD_ACK, mesh_view_u32(v, MESH_HTTP_HV_LOAD_SEQ));
    }
}

/* The load request words: refused (503) while meshd has not acked the last one. */
static uint32_t http_load_request(const mesh_ctx *ctx, const uint8_t *name, size_t n, uint32_t *slot) {
    mesh_view v = mesh_ctx_view(ctx), ack, sq;
    *slot = 0;
    uint32_t seq = mesh_view_u32(v, MESH_HTTP_HV_LOAD_SEQ);
    if (!mesh_view_sub(v, MESH_HTTP_HV_LOAD_ACK, 4, &ack)) return HTTP_RT_BAD;
    if (mesh_rt_ld_acquire(mesh_view_ptr(ack)) != seq) return HTTP_RT_BUSY;
    for (uint32_t i = 0; i < MESH_NAME_BYTES; i++) mesh_view_set_u8(v, MESH_HTTP_HV_LOAD_NAME + i, i < n ? name[i] : 0);
    mesh_rt_fence_acq_rel();
    if (mesh_view_sub(v, MESH_HTTP_HV_LOAD_SEQ, 4, &sq)) mesh_rt_st_release(mesh_view_ptr(sq), seq + 1);
    return HTTP_RT_LOAD;
}

/* Lane 0: the route and its slot, with /start, /stop and /load's side effects. */
static uint32_t http_parse(const mesh_ctx *ctx, const uint32_t req[64], size_t len, uint32_t *slot) {
    *slot = 0;
    /* "METHOD SP PATH[?query] SP VERSION" */
    size_t sp = 0;
    while (sp < len && http_byte(req, sp) != ' ') sp++;
    size_t ps = sp + 1, pe = ps;
    while (pe < len) {
        uint8_t c = http_byte(req, pe);
        if (c == ' ' || c == '\r' || c == '\n' || c == '?') break;
        pe++;
    }
    if (sp >= len || ps >= len || http_byte(req, ps) != '/') return HTTP_RT_BAD;
    if (!http_eq(req, 0, sp, "GET")) return HTTP_RT_NO_METHOD;
    static const char *const fixed_p[8] = {"/", "/programs", "/status", "/doom", "/train", "/render", "/render.ppm", "/render.bmp"};
    static const uint32_t fixed_r[8] = {HTTP_RT_INDEX, HTTP_RT_PROGRAMS, HTTP_RT_STATUS, HTTP_RT_DOOM,
                                        HTTP_RT_TRAIN, HTTP_RT_RENDER,   HTTP_RT_PPM,    HTTP_RT_BMP};
    for (int i = 0; i < 8; i++) {
        if (!http_eq(req, ps, pe, fixed_p[i])) continue;
        uint32_t r = fixed_r[i], s;
        if (r == HTTP_RT_RENDER || r == HTTP_RT_PPM || r == HTTP_RT_BMP) {
            if (mesh_ctx_find(ctx, "pathtrace", 9, &s) || mesh_ctx_find(ctx, "pathtrace-live", 14, &s)) { *slot = s; return r; }
            *slot = 1;
            return HTTP_RT_NOT_FOUND;
        }
        if (r == HTTP_RT_DOOM || r == HTTP_RT_TRAIN) {
            bool found = r == HTTP_RT_DOOM ? mesh_ctx_find(ctx, "doom", 4, &s) : mesh_ctx_find(ctx, "train", 5, &s);
            if (found) { *slot = s; return r; }
            *slot = 1;
            return HTTP_RT_NOT_FOUND;
        }
        return r;
    }
    static const char *const prefixes[4] = {"/start/", "/stop/", "/view/", "/load/"};
    for (uint32_t verb = 0; verb < 4; verb++) {
        if (!http_starts(req, ps, pe, prefixes[verb])) continue;
        size_t pl = 0;
        while (prefixes[verb][pl]) pl++;
        size_t at = ps + pl, n = pe - at;
        uint8_t name[MESH_NAME_BYTES] = {0};
        if (n == 0 || n > MESH_NAME_MAX) return verb == 3 ? HTTP_RT_BAD : HTTP_RT_NOT_FOUND;
        for (size_t i = 0; i < n; i++) name[i] = http_byte(req, at + i);
        uint32_t s;
        bool found = mesh_ctx_find(ctx, (const char *)name, (uint32_t)n, &s);
        if (verb == 3) {
            if (!mesh_valid_name(name, n)) return HTTP_RT_BAD;
            if (found) { *slot = s; return HTTP_RT_CONFLICT; }
            return http_load_request(ctx, name, n, slot);
        }
        if (!found) return HTTP_RT_NOT_FOUND;
        *slot = s;
        if (verb == 2) return HTTP_RT_VIEW;
        if (verb == 1 && s == mesh_ctx_slot(ctx)) return HTTP_RT_BAD; /* stopping http would end the server */
        mesh_ctx_request_run(ctx, s, verb == 0);
        return HTTP_RT_OK;
    }
    return HTTP_RT_NOT_FOUND;
}

/* Where doom's frame is: its own column arrays (local), or, placed on two
 * machines (pin, split), meshd's frame-matched copy in the show region -- the
 * current buffer, SH_SEQ & 1. (heights, kinds, sides, dists). All lanes call
 * it; lane 0's acquire of SH_SEQ picks the buffer for all. */
static void http_frame_arrays(mesh_view d, uint32_t at[4]) {
    at[0] = MESH_DOOM_D_HEIGHT, at[1] = MESH_DOOM_D_KIND, at[2] = MESH_DOOM_D_SIDE, at[3] = MESH_DOOM_D_DIST;
    if (mesh_view_len(d) < MESH_DOOM_SNAP_BYTES || mesh_view_u32(d, MESH_DOOM_D_SHOW + MESH_SH_MODE) == MESH_PLACE_LOCAL) return;
    uint32_t seq = mesh_rt_shfl_idx(mesh_rt_ld_acquire(mesh_view_ptr(d) + MESH_DOOM_D_SHOW + MESH_SH_SEQ), 0);
    if (seq == 0) return;
    uint32_t b = mesh_doom_show_data(seq & 1);
    at[0] = b + MESH_DOOM_SHOW_HEIGHT, at[1] = b + MESH_DOOM_SHOW_KIND, at[2] = b + MESH_DOOM_SHOW_SIDE, at[3] = b + MESH_DOOM_SHOW_DIST;
}

/* Wall span of screen column c on a DOOM_ROWS-row frame, and its shade (nearer
 * is denser; y-sides one shade darker). 0: no wall. */
static uint32_t http_column(mesh_view d, float rows, const uint32_t at[4], uint32_t c) {
    uint32_t kind = mesh_view_u32(d, at[1] + 4 * c);
    if (kind == 0 || !(rows > 0.0f)) return 0;
    float h = mesh_view_f32(d, at[0] + 4 * c) * (float)MESH_HTTP_DOOM_ROWS / rows;
    float dist = mesh_view_f32(d, at[3] + 4 * c);
    uint32_t side = mesh_view_u32(d, at[2] + 4 * c);
    float half = (float)MESH_HTTP_DOOM_ROWS / 2.0f;
    float t = half - h / 2.0f, bb = half + h / 2.0f;
    uint32_t top = t < 0.0f ? 0 : (uint32_t)t;
    uint32_t bot = bb > (float)MESH_HTTP_DOOM_ROWS ? MESH_HTTP_DOOM_ROWS : (uint32_t)bb;
    uint32_t shade = dist < 2.0f ? 0 : dist < 4.0f ? 1 : dist < 8.0f ? 2 : 3;
    if (side == 1 && shade < 3) shade++;
    static const uint8_t ch[4] = {'#', '%', '=', '-'};
    return (uint32_t)ch[shade] | top << 8 | bot << 16;
}

/* Width of the Content-Length value: filled in after the body, padded with
 * spaces (optional whitespace after a field value is valid HTTP). */
#define HTTP_CLEN_WIDTH 6u

static const char *http_state_name(uint32_t st) { return st < 5 ? MESH_STATES[st] : "?"; }

/* Tasks/s of slot s since the last page that sampled it (0 on the first). */
static float http_rate(const mesh_ctx *ctx, http_tab tab, uint32_t s) {
    mesh_view v = mesh_ctx_view(ctx);
    uint32_t at = MESH_HTTP_HV_SAMPLES + MESH_HTTP_SAMPLE_BYTES * s;
    uint32_t d0 = mesh_view_u32(v, at);
    uint64_t t0 = mesh_view_u64(v, at + 8);
    uint32_t d1 = tab_ctl(tab, s, MESH_P_DONE);
    uint64_t t1 = mesh_rt_gtimer();
    mesh_view_set_u32(v, at, d1);
    mesh_view_set_u64(v, at + 8, t1);
    return (t0 == 0 || t1 <= t0) ? 0.0f : (float)(d1 - d0) * 1e9f / (float)(t1 - t0);
}

/* "c0=v c1=v" for the named counters. */
static void http_counters(http_w *w, http_tab tab, uint32_t s) {
    bool first = true;
    for (uint32_t i = 0; i < MESH_COUNTERS; i++) {
        uint8_t n[16];
        http_bytes16(tab_ext(tab, s, MESH_PX_COUNTER_NAMES + MESH_NAME_BYTES * i), n);
        if (n[0] == 0) continue;
        if (!first) http_b(w, ' ');
        first = false;
        http_name(w, n);
        http_b(w, '=');
        http_u(w, tab_ctl(tab, s, MESH_P_COUNTERS + 4 * i));
    }
}

static void http_peer(http_w *w, http_tab tab) {
    uint32_t k = tab_hdr(tab, MESH_H_PEER), n = tab_hdr(tab, MESH_H_SYSTEMS);
    if (k < n) {
        http_s(w, "system ");
        http_u(w, k);
        http_s(w, " of ");
        http_u(w, n);
    } else http_s(w, "single");
}

/* (step u64, last loss, params) of the program named train, if loaded. */
static bool http_train_state(const mesh_ctx *ctx, uint64_t *step, float *loss, float p[4]) {
    uint32_t s;
    mesh_view t;
    if (!mesh_ctx_find(ctx, "train", 5, &s) || !mesh_ctx_peer_view(ctx, s, &t)) return false;
    *step = (uint64_t)mesh_view_u32(t, MESH_TRAIN_T_STEP_HI) << 32 | mesh_view_u32(t, MESH_TRAIN_T_NEXT_STEP);
    *loss = *step == 0 ? 0.0f : mesh_view_f32(t, MESH_TRAIN_T_LOSS + 4 * (uint32_t)((*step - 1) % 256));
    for (uint32_t k = 0; k < 4; k++) p[k] = mesh_view_f32(t, MESH_TRAIN_T_PARAMS + 4 * k);
    return true;
}

__attribute__((noinline)) static void http_index(http_w *w, const mesh_ctx *ctx, http_tab tab) {
    http_s(w, "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>alpha-rust-mesh</title>"
              "<style>body{font:14px monospace;background:#111;color:#ddd;margin:2em}a{color:#8cf}td,th{padding:0 1em 0 0;text-align:left}</style></head><body>\n"
              "<h1>alpha-rust-mesh</h1>\n<p>This page was routed and rendered by a GPU warp (program http, slot ");
    http_u(w, mesh_ctx_slot(ctx));
    http_s(w, ").</p>\n<p>");
    http_peer(w, tab);
    http_s(w, ", epoch ");
    http_u(w, tab_hdr(tab, MESH_H_EPOCH));
    http_s(w, "</p>\n<table>\n<tr><th>slot</th><th>program</th><th>state</th><th>nodes</th><th>tasks</th><th>tasks/s</th><th>counters</th><th></th></tr>\n");
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (!tab_used(tab, s)) continue;
        uint8_t n[16];
        tab_name(tab, s, n);
        http_s(w, "<tr><td>");
        http_u(w, s);
        http_s(w, "</td><td><a href=\"/view/");
        http_name(w, n);
        http_s(w, "\">");
        http_name(w, n);
        http_s(w, "</a></td><td>");
        http_s(w, http_state_name(tab_ent(tab, s, MESH_PT_STATE)));
        http_s(w, "</td><td>");
        http_u(w, tab_ctl(tab, s, MESH_P_ONLINE));
        http_b(w, '/');
        http_u(w, tab_ent(tab, s, MESH_PT_NODES));
        http_s(w, "</td><td>");
        http_u(w, tab_ctl(tab, s, MESH_P_DONE));
        http_s(w, "</td><td>");
        http_f(w, http_rate(ctx, tab, s), 1);
        http_s(w, "</td><td>");
        http_counters(w, tab, s);
        http_s(w, "</td><td><a href=\"/start/");
        http_name(w, n);
        http_s(w, "\">start</a> <a href=\"/stop/");
        http_name(w, n);
        http_s(w, "\">stop</a></td></tr>\n");
    }
    http_s(w, "</table>\n");
    uint64_t step;
    float loss, p[4];
    if (http_train_state(ctx, &step, &loss, p)) {
        http_s(w, "<p>train step ");
        http_u(w, step);
        http_s(w, ", loss ");
        http_f(w, loss, 6);
        http_s(w, ", params");
        for (int k = 0; k < 4; k++) { http_b(w, ' '); http_f(w, p[k], 6); }
        http_s(w, "</p>\n");
    }
    http_s(w, "<p><a href=\"/status\">/status</a> <a href=\"/programs\">/programs</a> <a href=\"/doom\">/doom</a> <a href=\"/train\">/train</a> "
              "<a href=\"/render\">/render</a> <a href=\"/render.bmp\">/render.bmp</a> | meshd: <a href=\"/fleet\">/fleet</a> <a href=\"/jobs\">/jobs</a> "
              "<a href=\"/pipes\">/pipes</a> (?format=json)</p>\n</body></html>\n");
}

/* One line per loaded slot: `slot name state online/nodes policy tasks tasks/s
 * counters`, then for a CPU executor `native pid P cpu% C rss_mb R` or `wasm
 * lanes L cpu% C rss_mb R` (the load page's PL_HOST). */
__attribute__((noinline)) static void http_programs(http_w *w, const mesh_ctx *ctx, http_tab tab) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (!tab_used(tab, s)) continue;
        uint8_t n[16];
        tab_name(tab, s, n);
        http_u(w, s);
        http_b(w, ' ');
        http_name(w, n);
        http_b(w, ' ');
        http_s(w, http_state_name(tab_ent(tab, s, MESH_PT_STATE)));
        http_b(w, ' ');
        http_u(w, tab_ctl(tab, s, MESH_P_ONLINE));
        http_b(w, '/');
        http_u(w, tab_ent(tab, s, MESH_PT_NODES));
        http_b(w, ' ');
        uint32_t pol = tab_ent(tab, s, MESH_PT_POLICY);
        http_s(w, pol < 4 ? MESH_POLICIES[pol] : "?");
        http_b(w, ' ');
        http_u(w, tab_ctl(tab, s, MESH_P_DONE));
        http_b(w, ' ');
        http_f(w, http_rate(ctx, tab, s), 1);
        http_b(w, ' ');
        http_counters(w, tab, s);
        /* A CPU executor (PX_EXEC): native worker process (its pid) or wasm lanes; CPU % (one core = 100) and RSS in MiB. */
        uint32_t exec = mesh_rt_ld_relaxed(tab_ext(tab, s, MESH_PX_EXEC));
        if (exec == MESH_BACKEND_NATIVE || exec == MESH_BACKEND_WASM) {
            size_t pl = mesh_pload(s);
            if (exec == MESH_BACKEND_NATIVE) {
                http_s(w, " native pid ");
                http_u(w, tab_ctl(tab, s, MESH_P_PID));
            } else {
                http_s(w, " wasm lanes ");
                http_u(w, tab_load(tab, pl + MESH_PL_UNITS));
            }
            http_s(w, " cpu% ");
            http_f(w, 100.0f * tab_loadf(tab, pl + MESH_PL_CPU), 1);
            http_s(w, " rss_mb ");
            http_u(w, ((uint64_t)tab_load(tab, pl + MESH_PL_RSS) | (uint64_t)tab_load(tab, pl + MESH_PL_RSS + 4) << 32) >> 20);
        }
        http_b(w, '\n');
    }
}

static void http_opt(http_w *w, const char *name, bool on, float v, uint32_t dec) {
    http_s(w, name);
    if (on) http_f(w, v, dec);
    else http_b(w, '-');
}

/* The load page (meshd's load sampler, about once a second): `key: value`
 * lines, `-` for what this machine does not measure. */
__attribute__((noinline)) static void http_load(http_w *w, http_tab tab) {
    uint32_t flags = tab_load(tab, MESH_S_LOAD_FLAGS);
    http_s(w, "load_samples: ");
    http_u(w, tab_load(tab, MESH_S_LOAD_SAMPLES));
    bool g = (flags & MESH_LF_GPU) != 0;
    http_opt(w, "\ngpu_share: ", g, tab_loadf(tab, MESH_S_GPU_SHARE), 3);
    http_opt(w, "\ngpu_share10: ", g, tab_loadf(tab, MESH_S_GPU_SHARE10), 3);
    http_opt(w, "\ngpu_slice_us: ", g, tab_loadf(tab, MESH_S_GPU_SLICE_US), 0);
    http_opt(w, "\ngpu_gap_us: ", g, tab_loadf(tab, MESH_S_GPU_GAP_US), 0);
    http_opt(w, "\ngpu_max_gap_us: ", true, tab_loadf(tab, MESH_S_GPU_MAX_GAP_US), 0);
    http_opt(w, "\ngpu_util_ext: ", (flags & MESH_LF_GPU_EXT) != 0, tab_loadf(tab, MESH_S_GPU_UTIL_EXT), 2);
    http_opt(w, "\ncpu: ", true, tab_loadf(tab, MESH_S_CPU_UTIL), 3);
    http_opt(w, "\ncpu_big: ", true, tab_loadf(tab, MESH_S_CPU_BIG), 3);
    http_opt(w, "\ncpu_little: ", tab_load(tab, MESH_S_CPUS_LITTLE) > 0, tab_loadf(tab, MESH_S_CPU_LITTLE), 3);
    http_opt(w, "\nloadavg: ", true, tab_loadf(tab, MESH_S_LOADAVG), 2);
    bool p = (flags & MESH_LF_PSI) != 0;
    http_opt(w, "\npsi_cpu: ", p, tab_loadf(tab, MESH_S_PSI_CPU), 4);
    http_opt(w, "\npsi_mem: ", p, tab_loadf(tab, MESH_S_PSI_MEM), 4);
    http_opt(w, "\npsi_io: ", p, tab_loadf(tab, MESH_S_PSI_IO), 4);
    http_s(w, "\nmem_avail_mb: ");
    http_u(w, tab_load(tab, MESH_S_MEM_AVAIL_MB));
    http_s(w, "\nmem_total_mb: ");
    http_u(w, tab_load(tab, MESH_S_MEM_TOTAL_MB));
    http_opt(w, "\nmeshd_cpu: ", true, tab_loadf(tab, MESH_S_MESHD_CPU), 3);
    http_opt(w, "\nmeshd_rss_mb: ", true, tab_loadf(tab, MESH_S_MESHD_RSS_MB), 1);
    http_s(w, "\ndisk_free_mb: ");
    http_u(w, tab_load(tab, MESH_S_DISK_FREE_MB));
    http_b(w, '\n');
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        size_t at = mesh_pload(s);
        uint32_t pf = tab_load(tab, at + MESH_PL_FLAGS);
        if ((pf & MESH_PLF_LOADED) == 0 || !tab_used(tab, s)) continue;
        uint8_t n[16];
        tab_name(tab, s, n);
        http_s(w, "program_load: ");
        http_name(w, n);
        http_opt(w, " share ", (pf & MESH_PLF_GPU) != 0, tab_loadf(tab, at + MESH_PL_SHARE10), 3);
        http_opt(w, " p50_us ", (pf & MESH_PLF_LAT) != 0, tab_loadf(tab, at + MESH_PL_P50_US), 1);
        http_opt(w, " p99_us ", (pf & MESH_PLF_LAT) != 0, tab_loadf(tab, at + MESH_PL_P99_US), 1);
        http_opt(w, " tasks_s ", true, tab_loadf(tab, at + MESH_PL_TASKS_S), 1);
        http_b(w, '\n');
    }
}

__attribute__((noinline)) static void http_status(http_w *w, const mesh_ctx *ctx, http_tab tab, uint32_t served) {
    uint32_t progs = 0, nodes = 0, online = 0, done = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (!tab_used(tab, s)) continue;
        progs++;
        nodes += tab_ent(tab, s, MESH_PT_NODES);
        online += tab_ctl(tab, s, MESH_P_ONLINE);
        done += tab_ctl(tab, s, MESH_P_DONE);
    }
    http_s(w, "programs: "); http_u(w, progs);
    http_s(w, "\nnodes: "); http_u(w, nodes);
    http_s(w, "\nonline: "); http_u(w, online);
    http_s(w, "\ndone: "); http_u(w, done);
    http_s(w, "\nepoch: "); http_u(w, tab_hdr(tab, MESH_H_EPOCH));
    http_s(w, "\npeer: "); http_peer(w, tab);
    uint64_t step;
    float loss, p[4];
    if (http_train_state(ctx, &step, &loss, p)) {
        http_s(w, "\ntrain_step: "); http_u(w, step);
        http_s(w, "\nloss: "); http_f(w, loss, 6);
    }
    http_s(w, "\nhttp_served: "); http_u(w, served);
    http_b(w, '\n');
    http_load(w, tab);
}

__attribute__((noinline)) static void http_train_page(http_w *w, const mesh_ctx *ctx) {
    uint64_t step;
    float loss, p[4];
    if (!http_train_state(ctx, &step, &loss, p)) { http_s(w, "not loaded\n"); return; }
    static const char *const names[4] = {"a: ", "b: ", "c: ", "d: "};
    for (int i = 0; i < 4; i++) { http_s(w, names[i]); http_f(w, p[i], 6); http_b(w, '\n'); }
    http_s(w, "step: "); http_u(w, step);
    http_s(w, "\nloss: "); http_f(w, loss, 6);
    http_b(w, '\n');
}

/* The declared fields of slot s: `name: value` lines. */
__attribute__((noinline)) static void http_view_page(http_w *w, const mesh_ctx *ctx, http_tab tab, uint32_t s) {
    uint8_t nm[16];
    tab_name(tab, s, nm);
    http_s(w, "program: "); http_name(w, nm);
    http_s(w, "\nstate: "); http_s(w, http_state_name(tab_ent(tab, s, MESH_PT_STATE)));
    http_s(w, "\nview_bytes: "); http_u(w, tab_ent(tab, s, MESH_PT_VIEW_BYTES));
    http_b(w, '\n');
    mesh_view pv;
    if (!mesh_ctx_peer_view(ctx, s, &pv)) return;
    uint32_t n = mesh_rt_rd(tab_ext(tab, s, MESH_PX_FIELD_COUNT));
    if (n > MESH_MAX_FIELDS) n = MESH_MAX_FIELDS;
    for (uint32_t f = 0; f < n; f++) {
        uint8_t *fd = tab_ext(tab, s, MESH_PX_FIELDS + MESH_FIELD_BYTES * f);
        uint8_t name[16];
        http_bytes16(fd, name);
        uint32_t ty = mesh_rt_rd(fd + MESH_FD_TYPE), off = mesh_rt_rd(fd + MESH_FD_OFF);
        http_name(w, name);
        /* names are up to 19 bytes: the tail past 16 */
        for (uint32_t i = 16; i < MESH_FD_NAME_MAX; i++) {
            uint8_t c = *(const volatile uint8_t *)(fd + i);
            if (c == 0) break;
            http_b(w, c);
        }
        http_s(w, ": ");
        switch (ty) {
        case MESH_FT_U32: http_u(w, mesh_view_u32(pv, off)); break;
        case MESH_FT_I32: http_i(w, (int32_t)mesh_view_u32(pv, off)); break;
        case MESH_FT_F32: http_f(w, mesh_view_f32(pv, off), 6); break;
        case MESH_FT_U64: http_u(w, mesh_view_u64(pv, off)); break;
        default: http_u(w, mesh_view_u8(pv, off)); break;
        }
        http_b(w, '\n');
    }
}

/* pathtrace's view, if it is one (its magic and size). */
static bool http_pt_view(const mesh_ctx *ctx, uint32_t slot, mesh_view *p) {
    return mesh_ctx_peer_view(ctx, slot, p) && mesh_view_len(*p) >= PT_VIEW_BYTES && mesh_view_u32(*p, PT_V_MAGIC) == PT_VIEW_MAGIC;
}

/* Lane 0: /render, pathtrace's progress. */
static void http_render_page(http_w *w, const mesh_ctx *ctx, http_tab tab, uint32_t slot) {
    mesh_view p;
    if (!http_pt_view(ctx, slot, &p)) { http_s(w, "pathtrace: no image yet (OP_INIT has not run)\n"); return; }
    uint8_t nm[16];
    tab_name(tab, slot, nm);
    http_name(w, nm);
    http_s(w, ": "); http_u(w, mesh_view_u32(p, PT_V_WIDTH)); http_b(w, 'x'); http_u(w, mesh_view_u32(p, PT_V_HEIGHT));
    http_s(w, ", "); http_u(w, mesh_view_u32(p, PT_V_TILES)); http_s(w, " tiles of 32x24, ");
    http_u(w, mesh_view_u32(p, PT_V_PASSES)); http_s(w, " passes of "); http_u(w, mesh_view_u32(p, PT_V_SPP)); http_s(w, " samples\n");
    http_s(w, "tiles done "); http_u(w, mesh_view_u32(p, PT_V_DONE));
    http_s(w, ", items "); http_u(w, mesh_view_u32(p, PT_V_ITEMS));
    http_s(w, ", scene gen "); http_u(w, mesh_view_u32(p, PT_V_GEN));
    http_s(w, ", tone map "); http_s(w, mesh_view_u32(p, PT_V_TONEMAP) == 1 ? "reinhard" : "aces");
    http_s(w, ", last tile "); http_u(w, mesh_view_u32(p, PT_V_LAST_TILE));
    http_s(w, "\nimage: /render.bmp /render.ppm\n");
}

static void http_le(http_w *w, uint32_t x, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) http_b(w, (uint8_t)(x >> (8 * i)));
}

/* /render.ppm, /render.bmp: the whole warp copies pathtrace's 8-bit pixels
 * (tile-major 0xffBBGGRR words) into the response, raster order (BMP: rows
 * bottom-up, BGR). False when there is no image or it does not fit: the text
 * path answers then. */
static bool http_image(const mesh_ctx *ctx, uint32_t route, uint32_t slot, uint32_t *code) {
    uint32_t l = mesh_ctx_lane(ctx);
    mesh_view p;
    if (!http_pt_view(ctx, slot, &p)) return false;
    uint32_t wd = mesh_view_u32(p, PT_V_WIDTH), ht = mesh_view_u32(p, PT_V_HEIGHT), tiles = mesh_view_u32(p, PT_V_TILES);
    mesh_view resp = mesh_ctx_response(ctx);
    uint32_t body = 3 * wd * ht;
    if (wd == 0 || ht == 0 || tiles != (wd / 32) * (ht / 24) || tiles > PT_MAX_TILES || body + 256 > mesh_view_len(resp)) return false;
    bool bmp = route == HTTP_RT_BMP;
    uint32_t head = 0;
    if (l == 0) {
        http_w w = {mesh_view_ptr(resp), 0, mesh_view_len(resp)};
        uint32_t file = bmp ? 54 + body : 0;
        http_s(&w, "HTTP/1.0 200 OK\r\nContent-Type: ");
        http_s(&w, bmp ? "image/bmp" : "image/x-portable-pixmap");
        http_s(&w, "\r\nContent-Length: ");
        uint32_t clen = w.n;
        for (uint32_t i = 0; i < HTTP_CLEN_WIDTH; i++) http_b(&w, ' ');
        http_s(&w, "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
        uint32_t at = w.n;
        if (bmp) {
            http_s(&w, "BM"); http_le(&w, file, 4); http_le(&w, 0, 4); http_le(&w, 54, 4);
            http_le(&w, 40, 4); http_le(&w, wd, 4); http_le(&w, ht, 4); http_le(&w, 1, 2); http_le(&w, 24, 2);
            http_le(&w, 0, 4); http_le(&w, body, 4); http_le(&w, 2835, 4); http_le(&w, 2835, 4); http_le(&w, 0, 4); http_le(&w, 0, 4);
        } else {
            http_s(&w, "P6\n"); http_u(&w, wd); http_b(&w, ' '); http_u(&w, ht); http_s(&w, "\n255\n");
        }
        head = w.n;
        http_w cl = {mesh_view_ptr(resp), clen, clen + HTTP_CLEN_WIDTH};
        http_u(&cl, head - at + body);
    }
    head = mesh_rt_shfl_idx(head, 0);
    uint32_t tx = wd / 32;
    for (uint32_t i = l; i < wd * ht; i += 32) {
        uint32_t yo = i / wd, x = i % wd;
        uint32_t y = bmp ? ht - 1 - yo : yo;
        uint32_t t = (y / 24) * tx + x / 32;
        uint32_t px = mesh_view_u32(p, pt_view_rgb(t) + 4 * ((y % 24) * 32 + x % 32));
        uint8_t *o = mesh_view_ptr(resp) + head + 3 * i;
        uint32_t a = bmp ? px >> 16 : px, b = bmp ? px : px >> 16;
        mesh_rt_wrb(o, (uint8_t)a);
        mesh_rt_wrb(o + 1, (uint8_t)(px >> 8));
        mesh_rt_wrb(o + 2, (uint8_t)b);
    }
    mesh_rt_fence_acq_rel();
    mesh_rt_warp_sync();
    mesh_view v = mesh_ctx_view(ctx);
    if (l == 0) {
        mesh_view_set_u32(v, MESH_HTTP_HV_SERVED, mesh_view_u32(v, MESH_HTTP_HV_SERVED) + 1);
        mesh_view_set_u32(v, MESH_HTTP_HV_LAST_CODE, 200);
    }
    mesh_ctx_respond(ctx, head + body);
    *code = 200;
    return true;
}

/* Lane 0: writes the response; its bytes, and the status code in *code. */
__attribute__((noinline)) static uint32_t http_render(const mesh_ctx *ctx, http_tab tab, uint32_t route, uint32_t slot,
                                                      const uint32_t cols[MESH_DOOM_WIDTH], uint32_t *code_out) {
    mesh_view resp = mesh_ctx_response(ctx), v = mesh_ctx_view(ctx);
    uint32_t code = 200;
    const char *reason = "OK", *ctype = "text/plain; charset=utf-8";
    switch (route) {
    case HTTP_RT_INDEX: ctype = "text/html; charset=utf-8"; break;
    case HTTP_RT_LOAD: code = 202, reason = "Accepted"; break;
    case HTTP_RT_BAD: code = 400, reason = "Bad Request"; break;
    case HTTP_RT_NOT_FOUND: code = 404, reason = "Not Found"; break;
    case HTTP_RT_NO_METHOD: code = 405, reason = "Method Not Allowed"; break;
    case HTTP_RT_CONFLICT: code = 409, reason = "Conflict"; break;
    case HTTP_RT_BUSY: code = 503, reason = "Service Unavailable"; break;
    default: break;
    }
    http_w w = {mesh_view_ptr(resp), 0, mesh_view_len(resp)};
    http_s(&w, "HTTP/1.0 "); http_u(&w, code); http_b(&w, ' '); http_s(&w, reason);
    http_s(&w, "\r\nContent-Type: "); http_s(&w, ctype);
    if (route == HTTP_RT_NO_METHOD) http_s(&w, "\r\nAllow: GET");
    http_s(&w, "\r\nContent-Length: ");
    uint32_t clen_at = w.n;
    for (uint32_t i = 0; i < HTTP_CLEN_WIDTH; i++) http_b(&w, ' ');
    http_s(&w, "\r\nConnection: close\r\n\r\n");
    uint32_t body_at = w.n;
    uint32_t served = mesh_view_u32(v, MESH_HTTP_HV_SERVED) + 1;
    switch (route) {
    case HTTP_RT_INDEX: http_index(&w, ctx, tab); break;
    case HTTP_RT_PROGRAMS: http_programs(&w, ctx, tab); break;
    case HTTP_RT_STATUS: http_status(&w, ctx, tab, served); break;
    case HTTP_RT_TRAIN: http_train_page(&w, ctx); break;
    case HTTP_RT_VIEW: http_view_page(&w, ctx, tab, slot); break;
    case HTTP_RT_RENDER: http_render_page(&w, ctx, tab, slot); break;
    case HTTP_RT_DOOM:
        for (uint32_t r = 0; r < MESH_HTTP_DOOM_ROWS; r++) {
            for (uint32_t c = 0; c < MESH_DOOM_WIDTH; c++) {
                uint32_t p = cols[c];
                uint8_t ch = (uint8_t)p;
                uint32_t top = (p >> 8) & 0xff, bot = (p >> 16) & 0xff;
                http_b(&w, (ch != 0 && r >= top && r < bot) ? ch : r < MESH_HTTP_DOOM_ROWS / 2 ? ' ' : '.');
            }
            http_b(&w, '\n');
        }
        break;
    case HTTP_RT_OK: http_s(&w, "ok\n"); break;
    case HTTP_RT_LOAD: http_s(&w, "load requested\n"); break;
    case HTTP_RT_BAD: http_s(&w, "bad request\n"); break;
    case HTTP_RT_NOT_FOUND: http_s(&w, slot == 1 ? "not loaded\n" : "not found\n"); break;
    case HTTP_RT_NO_METHOD: http_s(&w, "method not allowed\n"); break;
    case HTTP_RT_CONFLICT: http_s(&w, "already loaded\n"); break;
    default: http_s(&w, "a load request is pending\n"); break;
    }
    uint32_t total = w.n;
    http_w cl = {mesh_view_ptr(resp), clen_at, clen_at + HTTP_CLEN_WIDTH};
    http_u(&cl, total - body_at);
    mesh_view_set_u32(v, MESH_HTTP_HV_SERVED, served);
    mesh_view_set_u32(v, MESH_HTTP_HV_LAST_CODE, code);
    *code_out = code;
    return total;
}

/* Serves the request in the window; returns the HTTP status code. */
static uint32_t http_serve(const mesh_ctx *ctx) {
    uint32_t l = mesh_ctx_lane(ctx);
    mesh_view v = mesh_ctx_view(ctx), reqw;
    /* Without READS_ALL or the window (a manifest error) the reply is empty. */
    uint8_t *mb = mesh_ctx_mailbox(ctx);
    if (!mb || !mesh_view_sub(v, MESH_HTTP_REQ_OFF, MESH_HTTP_REQ_MAX, &reqw)) {
        mesh_ctx_respond(ctx, 0);
        return 0;
    }
    http_tab tab = {mb};
    /* Gather the request window: lane l loads words l and l + 32, every lane ends with all 64 words. */
    uint8_t *rp = mesh_view_ptr(reqw);
    uint32_t w0 = mesh_rt_rd(rp + 4 * l), w1 = mesh_rt_rd(rp + 4 * (l + 32));
    uint32_t req[64];
    for (uint32_t i = 0; i < 32; i++) {
        req[i] = mesh_rt_shfl_idx(w0, i);
        req[i + 32] = mesh_rt_shfl_idx(w1, i);
    }
    size_t len = mesh_view_len(mesh_ctx_request(ctx));

    uint32_t route = 0, slot = 0;
    if (l == 0) route = http_parse(ctx, req, len, &slot);
    route = mesh_rt_shfl_idx(route, 0);
    slot = mesh_rt_shfl_idx(slot, 0);

    /* The /doom frame, one packed column per entry: char | top << 8 | bottom << 16. */
    uint32_t cols[MESH_DOOM_WIDTH];
    for (uint32_t i = 0; i < MESH_DOOM_WIDTH; i++) cols[i] = 0;
    if (route == HTTP_RT_DOOM) {
        mesh_view d;
        if (mesh_ctx_peer_view(ctx, slot, &d)) {
            float rows = mesh_view_f32(d, MESH_DOOM_D_ROWS);
            uint32_t at[4];
            http_frame_arrays(d, at);
            for (uint32_t k = 0; k < MESH_DOOM_WIDTH / 32; k++) {
                uint32_t packed = http_column(d, rows, at, 32 * k + l);
                for (uint32_t i = 0; i < 32; i++) cols[32 * k + i] = mesh_rt_shfl_idx(packed, i);
            }
        }
    }
    uint32_t code = 0;
    if ((route == HTTP_RT_PPM || route == HTTP_RT_BMP) && http_image(ctx, route, slot, &code)) return code;
    uint32_t n = 0;
    if (l == 0) n = http_render(ctx, tab, route, slot, cols, &code);
    n = mesh_rt_shfl_idx(n, 0);
    mesh_ctx_respond(ctx, n);
    return mesh_rt_shfl_idx(code, 0);
}

static uint32_t http_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    (void)arg;
    if (op != MESH_OP_REQ) return MESH_R_BAD_OP;
    return http_serve(ctx);
}

MESH_PROGRAM(http_init, http_task);
