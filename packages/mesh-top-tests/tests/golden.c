/* Every frame of the Rust golden generator (the scratch tests/golden.rs, see docs/port/top.md)
 * replayed on the virtual clock and compared with data/frames.txt: the text of every cell and an
 * FNV-1a hash of every cell's colours and modifiers. The scenarios are crates/mesh-top's
 * tests/render.rs, cluster.rs, fleet.rs, views.rs, net.rs and load.rs fixtures, drawn at many
 * sizes, plus the Render tab. MT_GOLDEN_DUMP=<dir> writes the C frames and styles there. */
#include "mesh/check.h"
#include "mesh/top_mock.h"
#include "mesh/top_ui.h"
#include "pathtrace/view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T0 UINT64_C(1000000000000)

/* ---- the expected frames */

typedef struct {
    char *head;
    char *text;
} gframe;

static gframe *gold;
static size_t ngold, at, matched, failed;
static FILE *dump_frames, *dump_styles;

static char *slurp(const char *path, size_t *len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, fp) != (size_t)n) n = 0;
    b[n] = 0;
    fclose(fp);
    if (len) *len = (size_t)n;
    return b;
}

static void load_gold(void) {
    char *b = slurp("tests/data/frames.txt", NULL);
    CHECK(b != NULL);
    if (!b) return;
    size_t cap = 256;
    gold = malloc(cap * sizeof *gold);
    char *p = b;
    while (*p) {
        char *nl = strchr(p, '\n');
        if (!nl) break;
        *nl = 0;
        int w, h;
        const char *st = strstr(p, " style=");
        if (!st) break;
        const char *d = st - 1;
        while (d > p && *d != ' ') d--;
        if (sscanf(d + 1, "%dx%d", &w, &h) != 2) break;
        char *text = nl + 1, *q = text;
        for (int i = 0; i < h; i++) {
            char *e = strchr(q, '\n');
            q = e ? e + 1 : q + strlen(q);
        }
        if (ngold == cap) gold = realloc(gold, (cap *= 2) * sizeof *gold);
        gold[ngold].head = strdup(p);
        gold[ngold].text = strndup(text, (size_t)(q - text));
        ngold++;
        p = q;
    }
    free(b);
}

/* ---- frames */

static void fnv(uint64_t *h, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        *h ^= b[i];
        *h *= 0x100000001b3u;
    }
}

static void color_bytes(tui_color c, uint8_t out[4]) {
    memset(out, 0, 4);
    switch (c.kind) {
    case TUI_COLOR_RGB: out[0] = 1, out[1] = c.r, out[2] = c.g, out[3] = c.b; break;
    case TUI_COLOR_INDEXED: out[0] = 2, out[1] = c.r; break;
    case TUI_COLOR_NAMED: out[0] = 3, out[1] = c.r; break;
    default: break;
    }
}

static void frame(const char *name, const mt_app *app, uint16_t w, uint16_t h, mt_instant now) {
    tui_rect area = {0, 0, w, h};
    tui_buffer fb, out;
    tui_buffer_init(&fb, area);
    tui_buffer_init(&out, area);
    tui_arena a = {0};
    mt_ui_draw(&a, &fb, app, now);
    mt_ui_flush(&fb, &out);
    if (dump_styles) fprintf(dump_styles, "=== %s %ux%u\n", name, w, h);
    uint64_t hash = 0xcbf29ce484222325u;
    size_t cap = (size_t)w * h * 8 + h + 1, n = 0;
    char *text = malloc(cap);
    for (uint16_t y = 0; y < h; y++) {
        char run[64] = "";
        size_t rn = 0;
        for (uint16_t x = 0; x < w; x++) {
            const tui_cell *c = tui_buffer_cell_c(&out, x, y);
            const char *s = tui_cell_symbol(c);
            size_t l = strlen(s);
            if (n + l + 2 > cap) text = realloc(text, cap *= 2);
            memcpy(text + n, s, l);
            n += l;
            uint8_t fb4[4], bb4[4], mb[2] = {(uint8_t)c->mod, (uint8_t)(c->mod >> 8)};
            color_bytes(c->fg, fb4);
            fnv(&hash, fb4, 4);
            color_bytes(c->bg, bb4);
            fnv(&hash, bb4, 4);
            fnv(&hash, mb, 2);
            if (dump_styles) {
                char k[64];
                snprintf(k, sizeof k, "[%u, %u, %u, %u]/[%u, %u, %u, %u]/%x", fb4[0], fb4[1], fb4[2], fb4[3], bb4[0], bb4[1], bb4[2], bb4[3], c->mod);
                if (rn && strcmp(k, run) == 0) {
                    rn++;
                } else {
                    if (rn) fprintf(dump_styles, "%zu:%s ", rn, run);
                    snprintf(run, sizeof run, "%s", k);
                    rn = 1;
                }
            }
        }
        text[n++] = '\n';
        if (dump_styles) fprintf(dump_styles, "%zu:%s \n", rn, run);
    }
    text[n] = 0;
    char head[160];
    snprintf(head, sizeof head, "=== %s %ux%u style=%016llx", name, w, h, (unsigned long long)hash);
    if (dump_frames) fprintf(dump_frames, "%s\n%s", head, text);
    if (at < ngold) {
        const gframe *g = &gold[at];
        bool text_ok = strcmp(g->text, text) == 0, head_ok = strcmp(g->head, head) == 0;
        if (text_ok && head_ok) {
            matched++;
        } else {
            failed++;
            if (failed <= 12) {
                fprintf(stderr, "frame %zu differs: want %s\n                   got  %s%s\n", at, g->head, head,
                        text_ok ? " (text matches: styles differ)" : "");
                if (!text_ok) {
                    const char *p = g->text, *q = text;
                    for (int line = 0; *p || *q; line++) {
                        const char *pe = strchr(p, '\n'), *qe = strchr(q, '\n');
                        size_t pl = pe ? (size_t)(pe - p) : strlen(p), ql = qe ? (size_t)(qe - q) : strlen(q);
                        if (pl != ql || memcmp(p, q, pl) != 0) {
                            fprintf(stderr, "  line %d\n  want |%.*s|\n  got  |%.*s|\n", line, (int)pl, p, (int)ql, q);
                            break;
                        }
                        p = pe ? pe + 1 : p + pl;
                        q = qe ? qe + 1 : q + ql;
                    }
                }
            }
        }
    } else {
        failed++;
        fprintf(stderr, "frame %zu (%s) is past the golden file\n", at, head);
    }
    at++;
    free(text);
    tui_arena_free(&a);
    tui_buffer_free(&fb);
    tui_buffer_free(&out);
}

/* ---- helpers */

static void key(mt_app *app, tui_key k) {
    mt_action a = mt_app_handle_key(app, k);
    mt_action_free(&a);
}
static void kc(mt_app *app, uint32_t c) { key(app, tui_key_char(c)); }
static void kk(mt_app *app, tui_key_code c) { key(app, tui_key_of(c)); }

static void apply(mt_app *app, mt_update u, mt_instant now) { mt_app_apply(app, &u, now); }

static void endpoints(char **eps, size_t n, unsigned base, unsigned step) {
    for (size_t i = 0; i < n; i++) {
        eps[i] = malloc(32);
        snprintf(eps[i], 32, "127.0.0.1:%u", base + step * (unsigned)i);
    }
}
static void free_eps(char **eps, size_t n) {
    for (size_t i = 0; i < n; i++) free(eps[i]);
}

static void mk_app(mt_app *app, size_t n, unsigned base, unsigned step) {
    char *eps[8];
    endpoints(eps, n, base, step);
    mt_app_init(app, (const char *const *)eps, n);
    free_eps(eps, n);
}

/* ---------------------------------------------------------------- the render.rs fixture */

static void world(mt_mock_world *w, const char *host, uint32_t system) {
    mt_instant t = mt_now() - MT_SECS(20);
    mt_mock_world_init(w, host, system);
    w->step0 = t;
    w->started = t;
    w->has_frozen_step = true;
    w->frozen_step = 4000;
    w->http_served = 3;
    snprintf(w->last_http, sizeof w->last_http, "GET /hello HTTP/1.0");
    snprintf(w->stalled[0].name, sizeof w->stalled[0].name, "doom");
    w->stalled[0].node = 2;
    w->nstalled = 1;
}

static void app_with(mt_app *app, mt_mock_world *ws, size_t n) {
    mk_app(app, n, 7700, 10);
    mt_instant t0 = mt_now();
    for (size_t i = 0; i < n; i++) {
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0);
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0 + MT_MS(66));
        size_t len;
        uint8_t *tr = mt_mock_trace(&ws[i], &len);
        apply(app, mt_update_reply_bytes(i, "trace", tr, len), t0);
        free(tr);
        size_t total = 0;
        for (size_t k = 0; k < ws[i].nlog; k++) total += strlen(ws[i].log[k]) + 1;
        char *log = malloc(total + 1);
        size_t o = 0;
        for (size_t k = 0; k < ws[i].nlog; k++) o += (size_t)sprintf(log + o, "%s\n", ws[i].log[k]);
        apply(app, mt_update_reply_bytes(i, "log 200", log, o), t0);
        free(log);
    }
}

static void free_worlds(mt_mock_world *ws, size_t n) {
    for (size_t i = 0; i < n; i++) mt_mock_world_free(&ws[i]);
}

static const char *catalog_text(char *buf) {
    buf[0] = 0;
    for (int i = 0; i < MT_MOCK_CATALOG_LEN; i++) strcat(strcat(buf, MT_MOCK_CATALOG[i]), "\n");
    return buf;
}

/* A snapshot of w with one byte of the host block flipped. */
static mt_snapshot flipped(mt_mock_world *w, size_t host_off, uint8_t mask) {
    mt_snapshot s = mt_mock_synth(w);
    s.bytes[s.len - MESH_SNAP_HOST + host_off] ^= mask;
    mt_snapshot p;
    char *err = NULL;
    if (!mt_snapshot_parse(s.bytes, s.len, &p, &err)) abort();
    mt_snapshot_free(&s);
    return p;
}

static void scenario_render(void) {
    mt_clock_virtual(true, T0);
    mt_mock_world ws[2];
    world(&ws[0], "dgx-00", 0);
    world(&ws[1], "gx10-9959", 1);
    mt_app app;
    app_with(&app, ws, 2);
    mt_instant now = mt_now() + MT_MS(100);
    char name[64];
    for (int tab = 0; tab < MT_TABS; tab++) {
        app.tab = tab;
        const uint16_t sz[3][2] = {{160, 48}, {100, 30}, {60, 20}};
        snprintf(name, sizeof name, "render.tab%d", tab);
        for (int i = 0; i < 3; i++) frame(name, &app, sz[i][0], sz[i][1], now);
    }
    for (int tab = 0; tab < 4; tab++) {
        app.tab = tab;
        const uint16_t sz[4][2] = {{240, 60}, {80, 24}, {30, 8}, {12, 4}};
        snprintf(name, sizeof name, "render.tab%d", tab);
        for (int i = 0; i < 4; i++) frame(name, &app, sz[i][0], sz[i][1], now);
    }
    /* A stalled node after 1.5 s; the stale trace after 1 s. */
    app.tab = MT_TAB_MESH;
    frame("render.stall", &app, 160, 48, mt_now() + MT_MS(1500));
    app.tab = MT_TAB_PROGRAMS;
    frame("render.stale", &app, 160, 48, mt_now() + MT_MS(1000));
    /* Programs: selection on machine 1, the load prompt with the catalog, the unload prompt. */
    kc(&app, ']');
    kk(&app, TUI_KEY_DOWN);
    kk(&app, TUI_KEY_DOWN);
    frame("render.sel", &app, 160, 48, now);
    kc(&app, 'L');
    char cat[512];
    catalog_text(cat);
    apply(&app, mt_update_reply_bytes(1, "catalog", cat, strlen(cat)), now);
    for (int i = 0; i < 3; i++) kk(&app, TUI_KEY_DOWN);
    frame("render.load", &app, 160, 48, now);
    frame("render.load", &app, 100, 30, now);
    kk(&app, TUI_KEY_ESC);
    kc(&app, 'x');
    frame("render.unload", &app, 100, 30, now);
    kc(&app, 'n');
    frame("render.cancelled", &app, 100, 30, now);
    /* HTTP: typed path and a response, scrolled. */
    kc(&app, '[');
    kc(&app, '6');
    kk(&app, TUI_KEY_BACKSPACE);
    for (const char *c = "/stats"; *c; c++) kc(&app, (uint32_t)*c);
    size_t cap = 8192, bl = 0;
    char *body = malloc(cap);
    bl = (size_t)sprintf(body, "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nhello from the gpu\n");
    for (int i = 0; i < 40; i++)
        bl += (size_t)sprintf(body + bl, "line %02d: the GPU would compute this, and a long tail that wraps when the pane is narrow\n", i);
    apply(&app, mt_update_reply_bytes(0, "http /stats", body, bl), now);
    free(body);
    frame("render.http", &app, 160, 48, now);
    kk(&app, TUI_KEY_DOWN);
    kk(&app, TUI_KEY_PAGE_DOWN);
    frame("render.http", &app, 100, 30, now);
    /* Machine 1 offline. */
    apply(&app, mt_update_offline(1, "Connection refused"), now);
    app.tab = MT_TAB_MESH;
    frame("render.offline", &app, 160, 48, now);
    app.tab = MT_TAB_PROGRAMS;
    frame("render.offline", &app, 160, 48, now);
    /* A trace error stays in its pane. */
    apply(&app, mt_update_reply_text(0, "trace", "err unknown command trace"), now);
    frame("render.traceerr", &app, 160, 48, now);
    /* train: a different view prefix at the same step. */
    mt_mock_world w;
    world(&w, "gx10-9959", 1);
    mt_snapshot s = flipped(&w, MESH_S_LOCK_VIEW, 1);
    app.machines[1].nparams = 0;
    apply(&app, mt_update_snap(1, s), now);
    app.tab = MT_TAB_TRAIN;
    frame("render.identity", &app, 100, 30, now);
    mt_mock_world_free(&w);
    mt_app_free(&app);
    free_worlds(ws, 2);
}

static void scenario_doom_places(void) {
    const uint32_t modes[4][2] = {{MESH_PLACE_LOCAL, 0}, {MESH_PLACE_PIN, 0}, {MESH_PLACE_PIN, 1}, {MESH_PLACE_SPLIT, 0}};
    for (int mi = 0; mi < 4; mi++) {
        mt_clock_virtual(true, T0);
        mt_mock_world ws[2];
        world(&ws[0], "dgx-00", 0);
        world(&ws[1], "gx10-9959", 1);
        for (int i = 0; i < 2; i++) {
            ws[i].place[0] = modes[mi][0];
            ws[i].place[1] = modes[mi][1];
        }
        mt_app app;
        app_with(&app, ws, 2);
        mt_instant now = mt_now();
        app.tab = MT_TAB_DOOM;
        char name[64];
        for (size_t focus = 0; focus < 2; focus++) {
            app.focus = focus;
            snprintf(name, sizeof name, "doom.%u.%u.%zu", modes[mi][0], modes[mi][1], focus);
            frame(name, &app, 160, 48, now);
        }
        app.tab = MT_TAB_PROGRAMS;
        snprintf(name, sizeof name, "doom.%u.%u.programs", modes[mi][0], modes[mi][1]);
        frame(name, &app, 160, 48, now);
        mt_app_free(&app);
        free_worlds(ws, 2);
    }
}

static void scenario_single(void) {
    mt_clock_virtual(true, T0);
    mt_mock_world w;
    world(&w, "mac", 0);
    mt_mock_catalog_prog("gemm", 5, &w.progs[w.nprogs++]);
    mt_app app;
    app_with(&app, &w, 1);
    app.tab = MT_TAB_PROGRAMS;
    mt_instant now = mt_now();
    frame("single.gemm", &app, 170, 48, now);
    frame("single.gemm", &app, 100, 30, now);
    mt_app_free(&app);
    mt_mock_world_free(&w);

    mt_clock_virtual(true, T0);
    world(&w, "dgx-00", MESH_PEER_NONE);
    mt_mock_catalog_prog("life", 5, &w.progs[w.nprogs++]);
    app_with(&app, &w, 1);
    app.tab = MT_TAB_PROGRAMS;
    for (int i = 0; i < 10; i++) kk(&app, TUI_KEY_DOWN);
    now = mt_now();
    frame("single.life", &app, 160, 48, now);
    frame("single.life", &app, 100, 30, now);
    app.tab = MT_TAB_MESH;
    frame("single.mesh", &app, 100, 30, now);
    app.tab = MT_TAB_NET;
    frame("single.net", &app, 100, 30, now);
    mt_app_free(&app);
    mt_mock_world_free(&w);

    mt_clock_virtual(true, T0);
    world(&w, "dgx-00", MESH_PEER_NONE);
    size_t k = 0;
    for (size_t i = 0; i < w.nprogs; i++)
        if (strcmp(w.progs[i].name, "hello") == 0) w.progs[k++] = w.progs[i];
    w.nprogs = k;
    app_with(&app, &w, 1);
    now = mt_now();
    const int tabs[6] = {MT_TAB_DOOM, MT_TAB_TRAIN, MT_TAB_INFER, MT_TAB_HTTP, MT_TAB_RENDER, MT_TAB_LOAD};
    char name[64];
    for (int i = 0; i < 6; i++) {
        app.tab = tabs[i];
        snprintf(name, sizeof name, "single.absent%d", tabs[i]);
        frame(name, &app, 100, 30, now);
    }
    app.tab = MT_TAB_MESH;
    kc(&app, 'd');
    frame("single.note", &app, 100, 30, now);
    mt_app_free(&app);
    mt_mock_world_free(&w);
}

/* ---------------------------------------------------------------- cluster.rs */

static const char *const CHOSTS[4] = {"dgx-00", "gx10-9959", "being", "spare-3"};

static void cworlds(mt_mock_world *ws, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        mt_instant t = mt_now() - MT_SECS(20);
        mt_mock_world *w = &ws[k];
        mt_mock_world_init(w, CHOSTS[k], k);
        w->n = n;
        w->step0 = t;
        w->started = t;
        w->has_frozen_step = true;
        w->frozen_step = 4000;
        if (k == 2) {
            snprintf(w->gpu, sizeof w->gpu, "NVIDIA GeForce RTX 3060");
            w->cc = 86;
            w->blocks_per_sm = 16;
            snprintf(w->arch, sizeof w->arch, "x86_64");
        }
    }
}

static void capp(mt_app *app, mt_mock_world *ws, size_t n) {
    mk_app(app, n, 27700, 1);
    mt_instant t0 = mt_now();
    for (size_t i = 0; i < n; i++) {
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0);
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0 + MT_MS(66));
    }
}

static void scenario_cluster(void) {
    char name[64];
    for (uint32_t n = 3; n <= 4; n++) {
        mt_clock_virtual(true, T0);
        mt_mock_world ws[4];
        cworlds(ws, n);
        mt_app app;
        capp(&app, ws, n);
        mt_instant now = mt_now();
        const uint16_t sz[4][2] = {{200, 50}, {80, 24}, {240, 50}, {140, 40}};
        snprintf(name, sizeof name, "cluster%u.mesh", n);
        for (int i = 0; i < 4; i++) frame(name, &app, sz[i][0], sz[i][1], now);
        app.tab = MT_TAB_TRAIN;
        snprintf(name, sizeof name, "cluster%u.train", n);
        frame(name, &app, 200, 50, now);
        app.tab = MT_TAB_NET;
        snprintf(name, sizeof name, "cluster%u.net", n);
        frame(name, &app, 200, 50, now);
        app.tab = MT_TAB_LOAD;
        snprintf(name, sizeof name, "cluster%u.load", n);
        frame(name, &app, 200, 50, now);
        mt_app_free(&app);
        free_worlds(ws, n);
    }
    mt_clock_virtual(true, T0);
    mt_mock_world ws[3];
    cworlds(ws, 3);
    mt_app app;
    capp(&app, ws, 2);
    frame("cluster.viapeers", &app, 200, 50, mt_now());
    frame("cluster.viapeers", &app, 240, 50, mt_now());
    mt_app_free(&app);
    free_worlds(ws, 3);

    mt_clock_virtual(true, T0);
    cworlds(ws, 3);
    for (int i = 0; i < 3; i++) {
        ws[i].down[0] = 2;
        ws[i].ndown = 1;
    }
    capp(&app, ws, 2);
    frame("cluster.absent", &app, 200, 50, mt_now());
    mt_app_free(&app);
    free_worlds(ws, 3);

    mt_clock_virtual(true, T0);
    cworlds(ws, 3);
    capp(&app, ws, 3);
    app.tab = MT_TAB_DOOM;
    kc(&app, '[');
    frame("cluster.focus", &app, 200, 50, mt_now());
    /* identity NO across three */
    app.tab = MT_TAB_TRAIN;
    mt_snapshot s = flipped(&ws[2], MESH_S_LOCK_VIEW + 3, 0x80);
    app.machines[2].nparams = 0;
    apply(&app, mt_update_snap(2, s), mt_now());
    frame("cluster.identity", &app, 200, 50, mt_now());
    mt_app_free(&app);
    free_worlds(ws, 3);

    mt_clock_virtual(true, T0);
    cworlds(ws, 3);
    for (int i = 0; i < 3; i++) {
        ws[i].place[0] = MESH_PLACE_SPLIT;
        ws[i].place[1] = 0;
    }
    capp(&app, ws, 3);
    app.tab = MT_TAB_DOOM;
    for (size_t focus = 0; focus < 3; focus++) {
        app.focus = focus;
        snprintf(name, sizeof name, "cluster.split%zu", focus);
        frame(name, &app, 200, 50, mt_now());
    }
    mt_app_free(&app);
    free_worlds(ws, 3);

    mt_clock_virtual(true, T0);
    cworlds(ws, 3);
    for (int i = 0; i < 3; i++) {
        ws[i].place[0] = MESH_PLACE_PIN;
        ws[i].place[1] = 2;
    }
    capp(&app, ws, 3);
    app.tab = MT_TAB_DOOM;
    frame("cluster.pin2", &app, 200, 50, mt_now());
    frame("cluster.pin2", &app, 79, 30, mt_now());
    mt_app_free(&app);
    free_worlds(ws, 3);
}

/* ---------------------------------------------------------------- fleet.rs */

static const char *const FHOSTS[4] = {"dgx-00", "gx10-9959", "being", "mac-studio"};

static mt_snapshot fleet_snap(uint32_t k) {
    mt_mock_world w;
    mt_mock_world_init(&w, FHOSTS[k], k);
    w.n = 4;
    mt_snapshot s = mt_mock_synth(&w);
    mt_mock_world_free(&w);
    uint8_t *b = s.bytes;
    size_t h = s.len - MESH_SNAP_HOST;
    mt_put_u32(b, h + MESH_S_LEADER, 1);
    mt_put_u32(b, h + MESH_S_ROLE, k == 1 ? MESH_ROLE_LEADER : MESH_ROLE_FOLLOWER);
    mt_put_u64(b, h + MESH_S_TERM, 7);
    mt_put_u64(b, h + MESH_S_STATE_VER, 12);
    mt_put_u64(b, h + MESH_S_APPLIED_VER, k == 2 ? 11 : 12);
    mt_put_u32(b, h + MESH_S_UP_MASK, 7);
    mt_put_u32(b, h + MESH_S_SUSPECT_MASK, k == 0 ? 4 : 0);
    mt_put_u32(b, h + MESH_S_QUORUM, 3);
    mt_put_u32(b, h + MESH_S_FLEET_FLAGS, MESH_FF_QUORUM);
    mt_put_u32(b, h + MESH_S_JOBS_ACTIVE, 2);
    mt_put_u32(b, h + MESH_S_JOBS_TOTAL, 3);
    mt_put_u64(b, h + MESH_S_JOB_DONE, 12345);
    mt_put_u32(b, h + MESH_S_JOB_FLAGS, k == 1 ? MESH_JF_LEADER | MESH_JF_ACTIVE : 0);
    mt_put_u32(b, h + MESH_S_DEAD_MS, 3000);
    for (uint32_t j = 0; j < 4; j++) {
        size_t fo = h + mesh_fmember(j);
        uint32_t st = j == 3 ? MESH_RS_FAILED : j == 2 ? MESH_RS_PENDING : MESH_RS_SYNC;
        uint64_t ap = j == 3 ? 9 : j == 2 ? 11 : 12;
        const char *why = j == 3 ? "load http: unsupported" : j == 2 ? "reconciling version 12" : "ok";
        mt_put_u32(b, fo + MESH_FM_STATUS, st);
        mt_put_u32(b, fo + MESH_FM_GROUP, j == 3 ? MESH_GL_NONE : MESH_GL_PAUSED);
        mt_put_u64(b, fo + MESH_FM_APPLIED, ap);
        mt_put_u64(b, fo + MESH_FM_HEARD_MS, j == 3 ? 0 : 1000);
        mt_put_str(b, fo + MESH_FM_REASON, 40, why);
        mt_put_u32(b, h + mesh_member(j) + MESH_M_STATE, j == 3 ? MESH_MS_ABSENT : (j == 2 && k == 0) ? MESH_MS_SUSPECT : MESH_MS_UP);
        mt_put_str(b, h + mesh_member(j) + MESH_M_HOST, 32, FHOSTS[j]);
    }
    mt_put_str(b, h + MESH_SG_NAME, MESH_NAME_BYTES, "train");
    mt_put_u32(b, h + MESH_SG_EPOCH, 4);
    mt_put_u32(b, h + MESH_SG_CYCLE, 2);
    mt_put_u32(b, h + MESH_SG_MASK, 15);
    const uint8_t order[8] = {0, 1, 2, 3, 0xff, 0xff, 0xff, 0xff};
    memcpy(b + h + MESH_SG_ORDER, order, 8);
    mt_put_u32(b, h + MESH_SG_STATE, 2);
    mt_put_u32(b, h + MESH_SG_LOCAL, MESH_GL_PAUSED);
    mt_put_u32(b, h + MESH_SG_RANK, k);
    mt_put_u32(b, h + MESH_SG_ON_LOSS, 1);
    mt_put_u32(b, h + MESH_SG_AUTO_MS, 5000);
    mt_snapshot p;
    char *err = NULL;
    if (!mt_snapshot_parse(s.bytes, s.len, &p, &err)) abort();
    mt_snapshot_free(&s);
    return p;
}

static void scenario_fleet_snap(void) {
    mt_clock_virtual(true, T0);
    mt_app app;
    mk_app(&app, 4, 57900, 1);
    mt_instant t0 = mt_now();
    for (uint32_t k = 0; k < 3; k++) {
        apply(&app, mt_update_snap(k, fleet_snap(k)), t0);
        apply(&app, mt_update_snap(k, fleet_snap(k)), t0 + MT_MS(66));
    }
    kc(&app, '9');
    mt_instant now = mt_now();
    const uint16_t sz[4][2] = {{200, 50}, {100, 30}, {60, 12}, {30, 6}};
    for (int i = 0; i < 4; i++) frame("fleetsnap", &app, sz[i][0], sz[i][1], now);
    app.tab = MT_TAB_MESH;
    frame("fleetsnap.mesh", &app, 200, 50, now);
    mt_app_free(&app);
}

/* ---------------------------------------------------------------- views.rs */

static void scenario_views(void) {
    mt_clock_virtual(true, T0);
    mt_app app;
    mk_app(&app, 4, 64700, 1);
    mt_instant t0 = mt_now();
    const char *const views[3] = {"fleet", "jobs", "pipes"};
    for (int i = 0; i < 3; i++) {
        char path[64], line[32];
        snprintf(path, sizeof path, "tests/data/golden_%s.txt", views[i]);
        snprintf(line, sizeof line, "view %s", views[i]);
        size_t len;
        char *t = slurp(path, &len);
        CHECK(t != NULL);
        if (!t) continue;
        apply(&app, mt_update_reply_bytes(1, line, t, len), t0);
        free(t);
    }
    mt_instant now = t0 + MT_MS(400);
    kc(&app, '9');
    const uint16_t fs[6][2] = {{100, 30}, {200, 50}, {80, 24}, {60, 16}, {39, 8}, {12, 3}};
    for (int i = 0; i < 6; i++) frame("views.fleet", &app, fs[i][0], fs[i][1], now);
    kk(&app, TUI_KEY_DOWN);
    kk(&app, TUI_KEY_DOWN);
    frame("views.fleet.sel", &app, 100, 30, now);
    kk(&app, TUI_KEY_RIGHT);
    kk(&app, TUI_KEY_RIGHT);
    kc(&app, 'c');
    frame("views.fleet.confirm", &app, 100, 30, now);
    kc(&app, 'n');
    kc(&app, '0');
    const uint16_t js[4][2] = {{100, 30}, {200, 50}, {70, 20}, {39, 6}};
    for (int i = 0; i < 4; i++) frame("views.jobs", &app, js[i][0], js[i][1], now);
    kk(&app, TUI_KEY_DOWN);
    kc(&app, 'x');
    frame("views.jobs.cancel", &app, 100, 30, now);
    kk(&app, TUI_KEY_ESC);
    kc(&app, '-');
    const uint16_t ps[3][2] = {{100, 30}, {200, 50}, {39, 6}};
    for (int i = 0; i < 3; i++) frame("views.pipes", &app, ps[i][0], ps[i][1], now);
    const uint16_t ts[3][2] = {{140, 10}, {100, 10}, {40, 10}};
    for (int i = 0; i < 3; i++) {
        kc(&app, '0');
        frame("views.tabbar", &app, ts[i][0], ts[i][1], now);
    }
    mt_app_free(&app);
    /* Waiting and an error. */
    mt_clock_virtual(true, T0);
    const char *eps[2] = {"a:1", "b:2"};
    mt_app_init(&app, eps, 2);
    kc(&app, ']');
    kc(&app, '-');
    frame("views.waiting", &app, 100, 30, mt_now());
    apply(&app, mt_update_reply_text(1, "view pipes", "err the control port is busy"), mt_now());
    frame("views.err", &app, 100, 30, mt_now());
    kc(&app, '9');
    frame("views.fleetwait", &app, 100, 30, mt_now());
    frame("views.fleetwait", &app, 100, 11, mt_now());
    mt_app_free(&app);
}

/* ---------------------------------------------------------------- net.rs */

static const char *const NHOSTS[4] = {"dgx-00", "gx10-9959", "being", "monumentals-mac-studio.local"};

static void feed(mt_app *app, mt_mock_world *ws, size_t n, const uint64_t *times, size_t nt) {
    mt_instant t0 = mt_now();
    for (size_t q = 0; q < nt; q++)
        for (size_t i = 0; i < n; i++) {
            ws[i].has_clock_ms = true;
            ws[i].clock_ms = times[q];
            apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0);
        }
}

static void scenario_net(void) {
    mt_clock_virtual(true, T0);
    mt_mock_world ws[4];
    for (uint32_t k = 0; k < 4; k++) {
        mt_mock_world_init(&ws[k], NHOSTS[k], k);
        ws[k].n = 4;
        ws[k].has_frozen_step = true;
        ws[k].frozen_step = 4000;
        ws[k].place[0] = MESH_PLACE_SPLIT;
        ws[k].place[1] = 0;
    }
    mt_app app;
    mk_app(&app, 4, 7700, 10);
    const uint64_t times[3] = {1000, 2000, 3000};
    feed(&app, ws, 4, times, 3);
    mt_instant now = mt_now();
    const uint16_t ms[3][2] = {{220, 55}, {100, 30}, {160, 48}};
    for (int i = 0; i < 3; i++) frame("net.mesh", &app, ms[i][0], ms[i][1], now);
    app.tab = MT_TAB_NET;
    const uint16_t ns[3][2] = {{220, 55}, {100, 30}, {60, 14}};
    for (int i = 0; i < 3; i++) frame("net.net", &app, ns[i][0], ns[i][1], now);
    mt_app_free(&app);
    free_worlds(ws, 4);

    mt_clock_virtual(true, T0);
    mt_mock_world one;
    mt_mock_world_init(&one, "solo", MESH_PEER_NONE);
    mk_app(&app, 1, 7700, 10);
    feed(&app, &one, 1, times, 2);
    app.tab = MT_TAB_NET;
    frame("net.solo", &app, 100, 30, mt_now());
    app.tab = MT_TAB_MESH;
    frame("net.solomesh", &app, 100, 30, mt_now());
    mt_app_free(&app);
    mt_mock_world_free(&one);
}

/* ---------------------------------------------------------------- load.rs */

static void scenario_load(void) {
    mt_clock_virtual(true, T0);
    mt_mock_world ws[2];
    cworlds(ws, 2);
    mt_app app;
    capp(&app, ws, 2);
    mt_instant now = mt_now();
    frame("load.mesh", &app, 160, 60, now);
    frame("load.mesh", &app, 160, 40, now);
    kc(&app, '8');
    const uint16_t ls[4][2] = {{200, 50}, {100, 30}, {140, 30}, {19, 12}};
    for (int i = 0; i < 4; i++) frame("load.tab", &app, ls[i][0], ls[i][1], now);
    mt_app_free(&app);
    free_worlds(ws, 2);
    const char *ep = "127.0.0.1:1";
    mt_app_init(&app, &ep, 1);
    app.tab = MT_TAB_LOAD;
    frame("load.none", &app, 120, 30, now);
    app.tab = MT_TAB_MESH;
    frame("load.nonemesh", &app, 120, 30, now);
    mt_app_free(&app);
}

/* ---------------------------------------------------------------- render tab */

static void pathtrace_app(mt_app *app) {
    mt_mock_world w;
    world(&w, "dgx-00", 0);
    mt_snapshot s = mt_mock_synth(&w);
    mt_mock_world_free(&w);
    const uint8_t *head = s.bytes + MESH_SN_HEAD_BYTES;
    const uint8_t *host = s.bytes + s.len - MESH_SNAP_HOST;
    mt_section *secs = malloc((s.nsections + 1) * sizeof *secs);
    for (size_t i = 0; i < s.nsections; i++) secs[i] = s.sections[i];
    mt_section p = mt_section_new(6, 2, 0x5600);
    mt_put_str(mt_section_entry(&p), MESH_PT_NAME, MESH_NAME_BYTES, "pathtrace-live");
    mt_put_u32(mt_section_entry(&p), MESH_PT_STATE, MESH_ST_RUNNING);
    mt_put_u32(mt_section_entry(&p), MESH_PT_NODES, 2);
    mt_put_u32(mt_section_entry(&p), MESH_PT_LOADS, 1);
    mt_put_str(mt_section_ext(&p), MESH_PX_COUNTER_NAMES, MESH_NAME_BYTES, "items");
    uint32_t iw = 96, ih = 48, tiles = (iw / PT_TILE_W) * (ih / PT_TILE_H);
    uint8_t *vw = mt_section_view(&p);
    mt_put_u32(vw, PT_V_MAGIC, PT_VIEW_MAGIC);
    mt_put_u32(vw, PT_V_TILES, tiles);
    mt_put_u32(vw, PT_V_WIDTH, iw);
    mt_put_u32(vw, PT_V_HEIGHT, ih);
    mt_put_u32(vw, PT_V_SPP, 4);
    mt_put_u32(vw, PT_V_PASSES, 8);
    mt_put_u32(vw, PT_V_DONE, 2);
    mt_put_u32(vw, PT_V_TONEMAP, 1);
    mt_put_u32(vw, PT_V_GEN, 3);
    mt_put_u32(vw, PT_V_ITEMS, 29);
    for (uint32_t t = 0; t < tiles; t++) {
        uint32_t m = pt_view_meta(t);
        mt_put_u32(vw, m + PT_M_PASSES, t == 5 ? 0 : (t % 4) + 1 + (t == 2 ? PT_FOLDING : 0));
        mt_put_u32(vw, m + PT_M_SYSTEM, t < 3 ? 0 : MESH_PEER_NONE);
        mt_put_u32(vw, m + PT_M_EXEC, PT_EXEC_CUDA | (t % 3) << 8);
    }
    secs[s.nsections] = p;
    mt_snapshot a = mt_snapshot_assemble(head, secs, s.nsections + 1, host);
    mt_section_free(&p);
    free(secs);
    mt_snapshot_free(&s);
    const char *ep = "127.0.0.1:7700";
    mt_app_init(app, &ep, 1);
    mt_instant t0 = mt_now();
    apply(app, mt_update_snap(0, mt_snapshot_clone(&a)), t0);
    apply(app, mt_update_snap(0, a), t0 + MT_MS(66));
    size_t n = (size_t)tiles * PT_TILE_PIX * 4;
    uint8_t *px = malloc(n);
    for (size_t i = 0; i < n; i++) px[i] = (uint8_t)((i * 37 + i / 7 * 11) % 251);
    char line[64];
    snprintf(line, sizeof line, "peek pathtrace-live %u %zu", (unsigned)PT_V_RGB, n);
    apply(app, mt_update_reply_bytes(0, line, px, n), t0);
    free(px);
}

static uint8_t *fleet_image_bytes(uint64_t folded, size_t *len) {
    uint32_t iw = 128, ih = 72, tiles = (iw / PT_TILE_W) * (ih / PT_TILE_H);
    size_t cap = 1024 + 8 * tiles + (size_t)tiles * PT_TILE_PIX * 4;
    uint8_t *b = malloc(cap);
    int n = snprintf((char *)b, cap,
                     "render job 1.4 program pathtrace width %u height %u tiles_done 7 items 96 folded %llu passes 8 spp 4 tone aces system 1 "
                     "state running acc 9f2c pixels 3a7d bad 0 unavailable 1\ndigests sha256 "
                     "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\nmember 0 folded 30 tiles 5\nmember 1 folded 20 tiles "
                     "3\nmember 2 folded 10 tiles 4\n",
                     iw, ih, (unsigned long long)folded);
    size_t o = (size_t)n;
    b[o++] = 0;
    mt_put_u32(b, o, tiles);
    o += 4;
    for (uint32_t t = 0; t < tiles; t++) {
        mt_put_u32(b, o, t % 5);
        mt_put_u32(b, o + 4, t % 5 == 0 ? UINT32_MAX : t % 3);
        o += 8;
    }
    for (size_t i = 0; i < (size_t)tiles * PT_TILE_PIX * 4; i++) b[o++] = (uint8_t)((i * 13 + i / 31) % 241);
    *len = o;
    return b;
}

static void scenario_render_tab(void) {
    mt_clock_virtual(true, T0);
    mt_app app;
    pathtrace_app(&app);
    app.tab = MT_TAB_RENDER;
    mt_instant now = mt_now();
    const uint16_t vs[3][2] = {{100, 30}, {160, 48}, {40, 12}};
    for (int i = 0; i < 3; i++) frame("rtab.view", &app, vs[i][0], vs[i][1], now);
    kc(&app, 'm');
    frame("rtab.marks", &app, 100, 30, now);
    /* A fleet job's image, fetched twice (a rate). */
    mt_instant t0 = mt_now();
    size_t len;
    uint8_t *b = fleet_image_bytes(40, &len);
    apply(&app, mt_update_reply_bytes(0, "render latest raw", b, len), t0);
    free(b);
    b = fleet_image_bytes(60, &len);
    apply(&app, mt_update_reply_bytes(0, "render latest raw", b, len), t0 + MT_MS(400));
    free(b);
    frame("rtab.fleet", &app, 100, 30, now);
    frame("rtab.fleet", &app, 160, 48, now);
    kc(&app, 'm');
    frame("rtab.fleetplain", &app, 100, 30, now);
    mt_app_free(&app);
    /* The view without pixels yet, and an error. */
    mt_clock_virtual(true, T0);
    pathtrace_app(&app);
    mt_machine *m = &app.machines[0];
    m->has_render = false;
    free(m->render_name);
    free(m->render_px);
    m->render_name = NULL;
    m->render_px = NULL;
    m->render_len = 0;
    app.tab = MT_TAB_RENDER;
    frame("rtab.fetching", &app, 100, 30, mt_now());
    const char *e = "err peek out of the snap bytes";
    apply(&app, mt_update_reply_bytes(0, "peek pathtrace-live 1 2", e, strlen(e)), mt_now());
    frame("rtab.err", &app, 100, 30, mt_now());
    mt_app_free(&app);
}

int main(void) {
    load_gold();
    const char *dir = getenv("MT_GOLDEN_DUMP");
    if (dir) {
        char p[512];
        snprintf(p, sizeof p, "%s/frames.txt", dir);
        dump_frames = fopen(p, "w");
        snprintf(p, sizeof p, "%s/styles.txt", dir);
        dump_styles = fopen(p, "w");
    }
    scenario_render();
    scenario_doom_places();
    scenario_single();
    scenario_cluster();
    scenario_fleet_snap();
    scenario_views();
    scenario_net();
    scenario_load();
    scenario_render_tab();
    fprintf(stderr, "golden: %zu of %zu frames match (%zu differ)\n", matched, ngold, failed);
    CHECK_U(at, ngold);
    CHECK_U(failed, 0);
    if (dump_frames) fclose(dump_frames);
    if (dump_styles) fclose(dump_styles);
    for (size_t i = 0; i < ngold; i++) {
        free(gold[i].head);
        free(gold[i].text);
    }
    free(gold);
    return mesh_check_done();
}
