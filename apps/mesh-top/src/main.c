/* mesh-top (crates/mesh-top src/main.rs): `mesh-top [ENDPOINT ...]`: one background poller per
 * meshd endpoint (default 127.0.0.1:7700), the dashboard in the foreground.
 * `mesh-top --print TAB [WxH] [--ansi] [--marks] [ENDPOINT ...]`: polls for ~3 s, then prints one
 * frame of tab TAB (its key: 1-9, 0, -, =) as text, without a terminal. */
#include "mesh/top_client.h"
#include "mesh/top_ui.h"
#include "tui/render.h"
#include "tui/term.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ~15 Hz polling; the Doom tab polls at ~30 Hz so the game feels live. */
#define POLL_MS 66
#define POLL_DOOM_MS 33
/* Doom keys queued beyond this are dropped so held keys never lag behind. */
#define MAX_PENDING_KEYS 4

static mt_instant real_now(void) { return mt_now(); }

static void nap_ms(long ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}

static bool parse_u16(const char *s, size_t n, uint16_t *out) {
    if (n == 0 || n > 5) return false;
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    if (v > 65535) return false;
    *out = (uint16_t)v;
    return true;
}

/* Background refreshes (trace, log, views) never queue behind a command. */
static void background(mt_app *app, mt_endpoint **eps, size_t n) {
    bool idle[64];
    for (size_t i = 0; i < n && i < 64; i++) idle[i] = mt_endpoint_pending(eps[i]) == 0;
    mt_action a[2];
    size_t na = mt_app_tick(app, real_now(), a);
    for (size_t i = 0; i < na; i++) {
        if (a[i].kind == MT_ACTION_POLL && a[i].machine < n && idle[a[i].machine]) mt_endpoint_send(eps[a[i].machine], a[i].line);
        mt_action_free(&a[i]);
    }
}

static void drain(mt_app *app, mt_updates *q) {
    mt_update u;
    mt_instant now = real_now();
    while (mt_updates_pop(q, 0, &u)) mt_app_apply(app, &u, now);
}

static void print_frame(const mt_app *app, uint16_t w, uint16_t h, bool ansi) {
    tui_rect area = {0, 0, w, h};
    tui_buffer fb, out;
    tui_buffer_init(&fb, area);
    tui_buffer_init(&out, area);
    tui_arena a = {0};
    mt_ui_draw(&a, &fb, app, real_now());
    mt_ui_flush(&fb, &out);
    size_t cap = (size_t)w * 64 + 16;
    char *line = malloc(cap);
    for (uint16_t y = 0; y < h; y++) {
        size_t n = 0;
        for (uint16_t x = 0; x < w; x++) {
            const tui_cell *c = tui_buffer_cell_c(&out, x, y);
            if (ansi && c->fg.kind == TUI_COLOR_RGB) n += (size_t)snprintf(line + n, cap - n, "\x1b[38;2;%u;%u;%um", c->fg.r, c->fg.g, c->fg.b);
            if (ansi && c->bg.kind == TUI_COLOR_RGB) n += (size_t)snprintf(line + n, cap - n, "\x1b[48;2;%u;%u;%um", c->bg.r, c->bg.g, c->bg.b);
            n += (size_t)snprintf(line + n, cap - n, "%s", tui_cell_symbol(c));
        }
        if (ansi) {
            printf("%.*s\x1b[0m\n", (int)n, line);
        } else {
            while (n && (line[n - 1] == ' ' || (line[n - 1] >= '\t' && line[n - 1] <= '\r'))) n--;
            printf("%.*s\n", (int)n, line);
        }
    }
    free(line);
    tui_arena_free(&a);
    tui_buffer_free(&fb);
    tui_buffer_free(&out);
}

/* Handles one key; false to quit. */
static bool on_key(mt_app *app, mt_endpoint **eps, size_t n, tui_key k) {
    mt_action a = mt_app_handle_key(app, k);
    bool go = a.kind != MT_ACTION_QUIT;
    if (a.kind == MT_ACTION_SEND && a.machine < n) {
        bool is_key = strncmp(a.line, "input doom ", 11) == 0;
        if (!is_key || mt_endpoint_pending(eps[a.machine]) < MAX_PENDING_KEYS) mt_endpoint_send(eps[a.machine], a.line);
    }
    mt_action_free(&a);
    return go;
}

static int interactive(mt_app *app, mt_endpoint **eps, size_t n, mt_updates *q) {
    if (!tui_term_enter()) {
        fprintf(stderr, "mesh-top: stdin is not a terminal (try --print)\n");
        return 1;
    }
    tui_terminal term;
    tui_terminal_init(&term, 100, 30);
    char in[4096];
    size_t have = 0;
    bool go = true;
    while (go) {
        tui_arena a = {0};
        tui_buffer *buf = tui_terminal_begin(&term);
        mt_ui_draw(&a, buf, app, real_now());
        tui_terminal_end(&term);
        tui_arena_free(&a);
        /* Drain every queued key before the next frame. */
        long r = tui_term_read(in + have, sizeof in - have, 16);
        while (r > 0) {
            have += (size_t)r;
            r = have < sizeof in ? tui_term_read(in + have, sizeof in - have, 0) : 0;
        }
        if (r < 0 && have == 0) break;
        size_t at = 0;
        while (go && at < have) {
            tui_key k;
            size_t used = tui_input_parse(in + at, have - at, true, &k);
            if (used == 0) break;
            at += used;
            if (k.code != TUI_KEY_NONE) go = on_key(app, eps, n, k);
        }
        memmove(in, in + at, have - at);
        have -= at;
        uint64_t poll = app->tab == MT_TAB_DOOM ? POLL_DOOM_MS : POLL_MS;
        /* Only the focused machine needs fast frames. */
        for (size_t i = 0; i < n; i++) mt_endpoint_set_interval(eps[i], i == app->focus ? poll : POLL_MS);
        background(app, eps, n);
        drain(app, q);
    }
    tui_terminal_free(&term);
    tui_term_leave();
    return 0;
}

int main(int argc, char **argv) {
    const char **args = calloc((size_t)argc + 1, sizeof *args);
    size_t na = 0;
    bool ansi = false, marks = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: mesh-top [HOST:PORT ...]   (default 127.0.0.1:%u; one endpoint per machine of a cluster, any order)\n", MESH_CONTROL_PORT);
            printf("       mesh-top --print TAB [WxH] [--ansi] [--marks] [HOST:PORT ...]   (one frame of tab TAB, by its key 1-9 0 - =, as text; "
                   "--ansi: with its colours)\n");
            return 0;
        }
    }
    for (int i = 1; i < argc; i++) {
        /* --ansi (with --print): the frame with its colours; --marks: the Render tab's tiles marked. */
        if (!strcmp(argv[i], "--ansi")) ansi = true;
        else if (!strcmp(argv[i], "--marks")) marks = true;
        else args[na++] = argv[i];
    }
    bool print = false;
    int tab = 0;
    uint16_t pw = 100, ph = 30;
    size_t first = 0;
    if (na > 0 && !strcmp(args[0], "--print")) {
        int key = -1;
        if (na > 1 && args[1][0])
            for (int i = 0; i < MT_TABS; i++)
                if (args[1][0] == MT_TAB_KEYS[i]) key = i;
        if (key < 0) {
            fprintf(stderr, "mesh-top --print: TAB is a tab's key (1-9 0 - =):");
            for (int i = 0; i < MT_TABS; i++) fprintf(stderr, " %s", MT_TAB_NAMES[i]);
            fprintf(stderr, "\n");
            return 2;
        }
        print = true;
        tab = key;
        first = 2;
        const char *x = first < na ? strchr(args[first], 'x') : NULL;
        uint16_t w, h;
        if (x && parse_u16(args[first], (size_t)(x - args[first]), &w) && parse_u16(x + 1, strlen(x + 1), &h)) {
            pw = w;
            ph = h;
            first++;
        }
    }
    char def[32];
    snprintf(def, sizeof def, "127.0.0.1:%u", MESH_CONTROL_PORT);
    const char **endpoints = args + first;
    size_t n = na - first;
    if (n == 0) {
        endpoints = (const char *[]){def};
        n = 1;
    }
    mt_updates *q = mt_updates_new();
    mt_endpoint **eps = calloc(n, sizeof *eps);
    for (size_t i = 0; i < n; i++) eps[i] = mt_endpoint_spawn(i, endpoints[i], q, POLL_MS);
    mt_app app;
    mt_app_init(&app, endpoints, n);
    int rc = 0;
    if (print) {
        app.tab = tab;
        app.render_marks = marks;
        mt_instant t0 = real_now();
        while (real_now() - t0 < MT_SECS(3)) {
            background(&app, eps, n);
            drain(&app, q);
            nap_ms(20);
        }
        print_frame(&app, pw, ph, ansi);
    } else {
        rc = interactive(&app, eps, n, q);
    }
    for (size_t i = 0; i < n; i++) mt_endpoint_free(eps[i]);
    mt_updates_free(q);
    mt_app_free(&app);
    free(eps);
    free(args);
    return rc;
}
