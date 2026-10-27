/* What the ported crates/mesh-top tests share: a frame drawn as a TestBackend would hold it, its
 * text, keys, and the "contains" checks. The tests run on mesh-top-clock's virtual clock (the
 * Rust ones on the real clock, a few milliseconds apart: no assertion depends on it). */
#ifndef MESH_TOP_TESTS_SUPPORT_H
#define MESH_TOP_TESTS_SUPPORT_H

#include "mesh/check.h"
#include "mesh/top_mock.h"
#include "mesh/top_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T0 UINT64_C(1000000000000)

/* The frame flushed onto a blank screen; its text (rows joined by '\n') is returned. */
static inline char *render_buf(const mt_app *app, uint16_t w, uint16_t h, mt_instant now, tui_buffer *keep) {
    tui_rect area = {0, 0, w, h};
    tui_buffer fb, out;
    tui_buffer_init(&fb, area);
    tui_buffer_init(&out, area);
    tui_arena a = {0};
    mt_ui_draw(&a, &fb, app, now);
    mt_ui_flush(&fb, &out);
    size_t cap = (size_t)w * h * 8 + h + 1, n = 0;
    char *s = malloc(cap);
    for (uint16_t y = 0; y < h; y++) {
        for (uint16_t x = 0; x < w; x++) {
            const char *sym = tui_cell_symbol(tui_buffer_cell_c(&out, x, y));
            size_t l = strlen(sym);
            if (n + l + 2 > cap) s = realloc(s, cap *= 2);
            memcpy(s + n, sym, l);
            n += l;
        }
        s[n++] = '\n';
    }
    s[n] = 0;
    tui_arena_free(&a);
    tui_buffer_free(&fb);
    if (keep) *keep = out;
    else tui_buffer_free(&out);
    return s;
}

static inline char *render(const mt_app *app, uint16_t w, uint16_t h) { return render_buf(app, w, h, mt_now(), NULL); }

/* Every string in a NULL-terminated list is in s (each miss is reported with the frame once). */
static inline bool has_all(const char *s, const char *const *want, const char *what) {
    bool ok = true;
    for (; *want; want++)
        if (!strstr(s, *want)) {
            fprintf(stderr, "%s: missing \"%s\"\n", what, *want);
            ok = false;
        }
    if (!ok) fprintf(stderr, "%s\n", s);
    CHECK(ok);
    return ok;
}
#define HAS(s, what, ...) has_all((s), (const char *const[]){__VA_ARGS__, NULL}, (what))

/* Renders, checks, frees. */
#define RHAS(app, w, h, ...)                                    \
    do {                                                        \
        char *rs_ = render((app), (w), (h));                    \
        char what_[64];                                         \
        snprintf(what_, sizeof what_, "%dx%d", (w), (h));       \
        HAS(rs_, what_, __VA_ARGS__);                           \
        free(rs_);                                              \
    } while (0)

static inline size_t count_of(const char *s, const char *needle) {
    size_t n = 0, l = strlen(needle);
    for (const char *p = strstr(s, needle); p; p = strstr(p + l, needle)) n++;
    return n;
}

/* The line of s holding every one of a and b (NULL when none); malloc'd. */
static inline char *line_with(const char *s, const char *a, const char *b) {
    while (*s) {
        const char *e = strchr(s, '\n');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        char *line = strndup(s, l);
        if (strstr(line, a) && (!b || strstr(line, b))) return line;
        free(line);
        s += l + (e != NULL);
    }
    return NULL;
}

static inline mt_action key_ev(mt_app *app, tui_key k) { return mt_app_handle_key(app, k); }
static inline mt_action keyc(mt_app *app, uint32_t c) { return mt_app_handle_key(app, tui_key_char(c)); }
static inline mt_action keyk(mt_app *app, tui_key_code c) { return mt_app_handle_key(app, tui_key_of(c)); }
static inline void press(mt_app *app, mt_action a) {
    (void)app;
    mt_action_free(&a);
}
#define KC(app, c) press((app), keyc((app), (c)))
#define KK(app, c) press((app), keyk((app), (c)))

/* The action is a Send of line to machine m (and is freed). */
static inline bool sent_is(mt_action a, size_t m, const char *line) {
    bool ok = a.kind == MT_ACTION_SEND && a.machine == m && a.line && strcmp(a.line, line) == 0;
    if (!ok) fprintf(stderr, "expected Send(%zu, \"%s\"), got kind %d (%zu, \"%s\")\n", m, line, a.kind, a.machine, a.line ? a.line : "");
    mt_action_free(&a);
    return ok;
}
static inline bool is_none(mt_action a) {
    bool ok = a.kind == MT_ACTION_NONE;
    mt_action_free(&a);
    return ok;
}

static inline void apply(mt_app *app, mt_update u, mt_instant now) { mt_app_apply(app, &u, now); }

static inline void mk_app(mt_app *app, size_t n, unsigned base, unsigned step) {
    char eps[8][32];
    const char *p[8];
    for (size_t i = 0; i < n; i++) {
        snprintf(eps[i], sizeof eps[i], "127.0.0.1:%u", base + step * (unsigned)i);
        p[i] = eps[i];
    }
    mt_app_init(app, p, n);
}

/* A snapshot of w with one byte of the host block flipped. */
static inline mt_snapshot flipped(mt_mock_world *w, size_t host_off, uint8_t mask) {
    mt_snapshot s = mt_mock_synth(w);
    s.bytes[s.len - MESH_SNAP_HOST + host_off] ^= mask;
    mt_snapshot p;
    char *err = NULL;
    if (!mt_snapshot_parse(s.bytes, s.len, &p, &err)) abort();
    mt_snapshot_free(&s);
    return p;
}

/* Re-parses edited snapshot bytes (false with *err set when refused). */
static inline bool reparse(const uint8_t *b, size_t len, mt_snapshot *out, char **err) {
    *err = NULL;
    return mt_snapshot_parse(b, len, out, err);
}

static inline bool color_is(tui_color c, uint8_t r, uint8_t g, uint8_t b) { return c.kind == TUI_COLOR_RGB && c.r == r && c.g == g && c.b == b; }

#endif
