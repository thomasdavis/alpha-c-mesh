/* crates/mesh-top tests/render.rs, ported: every tab rendered from synthetic snapshots, plus the
 * key bindings that turn into control lines. */
#include "support.h"

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

/* Machines each fed two snapshots (so rates and stall tracking have a delta) and a trace and a log. */
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
        size_t total = 1, o = 0;
        for (size_t k = 0; k < ws[i].nlog; k++) total += strlen(ws[i].log[k]) + 1;
        char *log = malloc(total);
        for (size_t k = 0; k < ws[i].nlog; k++) o += (size_t)sprintf(log + o, "%s\n", ws[i].log[k]);
        apply(app, mt_update_reply_bytes(i, "log 200", log, o), t0);
        free(log);
    }
}

typedef struct {
    mt_mock_world ws[2];
    mt_app app;
} two;

static void two_new(two *t) {
    world(&t->ws[0], "dgx-00", 0);
    world(&t->ws[1], "gx10-9959", 1);
    app_with(&t->app, t->ws, 2);
}

static void two_free(two *t) {
    mt_app_free(&t->app);
    mt_mock_world_free(&t->ws[0]);
    mt_mock_world_free(&t->ws[1]);
}

static void mesh_tab(void) {
    two t;
    two_new(&t);
    const uint16_t sz[2][2] = {{160, 48}, {100, 30}};
    for (int i = 0; i < 2; i++) {
        char *s = render_buf(&t.app, sz[i][0], sz[i][1], mt_now() + MT_MS(100), NULL);
        HAS(s, "mesh tab", "dgx-00", "gx10-9959", "NVIDIA GB10", "[doom]", "[hello]", "peered", "nodes by program", "blinks");
        free(s);
    }
    char *s = render(&t.app, 160, 48);
    HAS(s, "mesh tab", "9/9 online");
    free(s);
    /* After > 1 s without heartbeat change, doom's frozen node 2 is red; others are not. */
    const mt_machine *m = &t.app.machines[0];
    CHECK(mt_machine_stalled(m, 0, 2, mt_now() + MT_MS(1500)) && !mt_machine_stalled(m, 0, 2, mt_now()));
    CHECK(!mt_machine_stalled(m, 0, 1, mt_now() + MT_MS(500)));
    two_free(&t);
}

static void mesh_offline_panel(void) {
    two t;
    two_new(&t);
    apply(&t.app, mt_update_offline(1, "Connection refused"), mt_now());
    RHAS(&t.app, 160, 48, "OFFLINE", "Connection refused");
    two_free(&t);
}

static void programs_tab_table_view_flame_log(void) {
    two t;
    two_new(&t);
    CHECK(is_none(keyc(&t.app, '2')));
    CHECK_U(t.app.tab, MT_TAB_PROGRAMS);
    RHAS(&t.app, 160, 48, "slot", "tasks/s", "p50 µs", "doom", "train", "hello", "running", "view doom", "trace", "log", "load hello");
    RHAS(&t.app, 100, 30, "slot", "tasks/s", "p50 µs", "doom", "train", "hello", "running", "view doom", "trace", "log", "load hello");
    tui_buffer buf;
    char *s = render_buf(&t.app, 160, 48, mt_now(), &buf);
    /* Both machines' tables, the ptx hash short, the doom fields, a hexdump row, flame lanes. */
    HAS(s, "programs", "dgx-00", "gx10-9959", "pos_x", "frame", "frames", "0000 ", "doom/0", "-100ms", "now", "periodic");
    char hash[16];
    snprintf(hash, sizeof hash, "%08llx", (unsigned long long)(mesh_fnv1a64((const uint8_t *)"doom", 4) >> 32));
    HAS(s, "hash", hash);
    /* The flame chart draws doom's tasks in doom's colour. */
    size_t glyphs = 0;
    for (size_t i = 0; i < (size_t)160 * 48; i++)
        if (color_is(buf.cells[i].fg, 255, 110, 60) && strstr("▁▂▃▄▅▆▇█", tui_cell_symbol(&buf.cells[i])) && strcmp(tui_cell_symbol(&buf.cells[i]), " "))
            glyphs++;
    CHECK(glyphs > 0);
    /* p50 of hello's synthetic 7 µs tasks shows in its row. */
    char *row = line_with(s, "hello", "on_demand");
    CHECK(row && (strstr(row, "6.") || strstr(row, "7.0")));
    free(row);
    free(s);
    tui_buffer_free(&buf);
    two_free(&t);
}

static void programs_tab_generic_view_for_unknown_program(void) {
    mt_mock_world a;
    world(&a, "dgx-00", MESH_PEER_NONE);
    mt_mock_catalog_prog("life", 5, &a.progs[a.nprogs++]);
    mt_app app;
    app_with(&app, &a, 1);
    app.tab = MT_TAB_PROGRAMS;
    for (int i = 0; i < 10; i++) KK(&app, TUI_KEY_DOWN);
    CHECK(mt_section_name_is(mt_app_selected(&app), "life"));
    RHAS(&app, 160, 48, "view life", "generation", "density", "0.290000", "seed", "rule", "23", "generations", "births", "0020 ");
    RHAS(&app, 100, 30, "view life", "generation", "density", "0.290000", "seed", "rule", "23", "generations", "births", "0020 ");
    KK(&app, TUI_KEY_UP);
    CHECK(mt_section_name_is(mt_app_selected(&app), "hello"));
    mt_app_free(&app);
    mt_mock_world_free(&a);
}

static void programs_keys(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    app->tab = MT_TAB_PROGRAMS;
    /* Row 0 is doom (running): s stops it; u/b/r act on it by name. */
    CHECK(sent_is(keyc(app, 's'), 0, "stop doom"));
    KK(app, TUI_KEY_DOWN);
    CHECK(sent_is(keyc(app, 'u'), 0, "upgrade train"));
    CHECK(sent_is(keyc(app, 'b'), 0, "rollback train"));
    CHECK(sent_is(keyc(app, 'r'), 0, "reset train"));
    /* x asks first: anything but y cancels; y unloads. */
    CHECK(is_none(keyc(app, 'x')));
    CHECK(app->prompt.kind == MT_PROMPT_UNLOAD && app->prompt.machine == 0 && strcmp(app->prompt.name, "train") == 0);
    RHAS(app, 100, 30, "unload train on dgx-00?");
    CHECK(is_none(keyc(app, 'n')));
    CHECK(app->prompt.kind == MT_PROMPT_NONE && strstr(app->last_reply, "cancelled"));
    KC(app, 'x');
    CHECK(sent_is(keyc(app, 'y'), 0, "unload train"));
    /* A stopped program: s starts it. */
    mt_mock_world w;
    world(&w, "dgx-00", 0);
    w.progs[1].state = MESH_ST_LOADED;
    apply(app, mt_update_snap(0, mt_mock_synth(&w)), mt_now());
    mt_mock_world_free(&w);
    CHECK(sent_is(keyc(app, 's'), 0, "start train"));
    /* The selection follows the focused machine. */
    KC(app, ']');
    CHECK(sent_is(keyc(app, 's'), 1, "stop train"));
    two_free(&t);
}

static void load_prompt_lists_the_catalog(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    app->tab = MT_TAB_PROGRAMS;
    CHECK(sent_is(keyc(app, 'L'), 0, "catalog"));
    char cat[512] = "";
    for (int i = 0; i < MT_MOCK_CATALOG_LEN; i++) strcat(strcat(cat, MT_MOCK_CATALOG[i]), "\n");
    apply(app, mt_update_reply_bytes(0, "catalog", cat, strlen(cat)), mt_now());
    RHAS(app, 160, 48, "load a program", "catalog (7)", "life", "loaded");
    RHAS(app, 100, 30, "load a program", "catalog (7)", "life", "loaded");
    /* Pick with the arrows (doom, gemm, hello, http, infer, life, ...), or type a dir. */
    for (int i = 0; i < 6; i++) KK(app, TUI_KEY_DOWN);
    CHECK(sent_is(keyk(app, TUI_KEY_ENTER), 0, "load life"));
    KC(app, 'l');
    for (const char *c = "x/y"; *c; c++) KC(app, (uint32_t)*c);
    /* q and digits are text in the prompt, not quit / tab. */
    KC(app, 'q');
    KK(app, TUI_KEY_BACKSPACE);
    CHECK(sent_is(keyk(app, TUI_KEY_ENTER), 0, "load x/y"));
    KC(app, 'L');
    CHECK(is_none(keyk(app, TUI_KEY_ESC)));
    CHECK(app->prompt.kind == MT_PROMPT_NONE && app->tab == MT_TAB_PROGRAMS);
    two_free(&t);
}

static size_t tick(mt_app *app, mt_instant t, mt_action out[2]) { return mt_app_tick(app, t, out); }
static void free_actions(mt_action *a, size_t n) {
    for (size_t i = 0; i < n; i++) mt_action_free(&a[i]);
}

static void programs_tab_polls_trace_and_log_on_focus(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    mt_instant t0 = mt_now();
    mt_action a[2];
    size_t n = tick(app, t0, a);
    CHECK_U(n, 0); /* no background polls off the Programs tab */
    free_actions(a, n);
    app->tab = MT_TAB_PROGRAMS;
    n = tick(app, t0, a);
    CHECK_U(n, 2);
    CHECK(n == 2 && a[0].kind == MT_ACTION_POLL && a[0].machine == 0 && !strcmp(a[0].line, "trace"));
    CHECK(n == 2 && a[1].kind == MT_ACTION_POLL && a[1].machine == 0 && !strcmp(a[1].line, "log 200"));
    free_actions(a, n);
    n = tick(app, t0 + MT_MS(100), a);
    CHECK_U(n, 0);
    free_actions(a, n);
    app->focus = 1;
    n = tick(app, t0 + MT_AUX_POLL, a);
    CHECK(n > 0 && a[0].kind == MT_ACTION_POLL && a[0].machine == 1);
    free_actions(a, n);
    /* An error reply stays in its pane, not the status line. */
    apply(app, mt_update_reply_text(1, "trace", "err unknown command trace"), t0);
    free(app->last_reply);
    app->last_reply = strdup("");
    char *s = render_buf(app, 160, 48, t0, NULL);
    HAS(s, "trace error", "trace: err unknown command trace");
    CHECK(app->last_reply[0] == 0);
    free(s);
    two_free(&t);
}

static void doom_tab_draws_walls_and_sends_input(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    CHECK(is_none(keyc(app, '3')));
    CHECK_U(app->tab, MT_TAB_DOOM);
    tui_buffer buf;
    char *s = render_buf(app, 160, 48, mt_now(), &buf);
    HAS(s, "doom", "fps", "map", "▀");
    CHECK(tui_buffer_cell(&buf, 60, 24)->fg.kind == TUI_COLOR_RGB);
    free(s);
    tui_buffer_free(&buf);
    /* q is strafe in Doom, not quit; arrows map to w/s/a/d. */
    CHECK(sent_is(keyc(app, 'q'), 0, "input doom q"));
    CHECK(sent_is(keyk(app, TUI_KEY_UP), 0, "input doom w"));
    CHECK(sent_is(keyk(app, TUI_KEY_LEFT), 0, "input doom a"));
    CHECK(sent_is(keyc(app, 'E'), 0, "input doom e"));
    CHECK(is_none(keyk(app, TUI_KEY_ESC)));
    CHECK_U(app->tab, MT_TAB_MESH);
    free(render(app, 100, 30));
    two_free(&t);
}

static void custom_tabs_say_not_loaded(void) {
    mt_mock_world w;
    world(&w, "dgx-00", MESH_PEER_NONE);
    size_t k = 0;
    for (size_t i = 0; i < w.nprogs; i++)
        if (!strcmp(w.progs[i].name, "hello")) w.progs[k++] = w.progs[i];
    w.nprogs = k;
    mt_app app;
    app_with(&app, &w, 1);
    const int tabs[4] = {MT_TAB_DOOM, MT_TAB_TRAIN, MT_TAB_INFER, MT_TAB_HTTP};
    const char *names[4] = {"doom", "train", "infer", "http"};
    for (int i = 0; i < 4; i++) {
        app.tab = tabs[i];
        char want[64];
        snprintf(want, sizeof want, "%s is not loaded on dgx-00", names[i]);
        RHAS(&app, 160, 48, want);
        RHAS(&app, 100, 30, want);
    }
    /* d/t/i on an absent program say so instead of sending. */
    app.tab = MT_TAB_MESH;
    CHECK(is_none(keyc(&app, 'd')));
    CHECK(strstr(app.last_reply, "doom is not loaded") != NULL);
    mt_app_free(&app);
    mt_mock_world_free(&w);
}

static void train_tab_shows_identity(void) {
    two t;
    two_new(&t);
    t.app.tab = MT_TAB_TRAIN;
    RHAS(&t.app, 160, 48, "steps/s", "train params bitwise identical across 2: YES (at step 4000)", "T_ERR", "1e", "running");
    /* A different view prefix at the same step: NO. */
    mt_mock_world w;
    world(&w, "gx10-9959", 1);
    mt_snapshot s = flipped(&w, MESH_S_LOCK_VIEW, 1);
    mt_mock_world_free(&w);
    t.app.machines[1].nparams = 0;
    apply(&t.app, mt_update_snap(1, s), mt_now());
    RHAS(&t.app, 100, 30, "train params bitwise identical across 2: NO (at step 4000)");
    two_free(&t);
}

static void infer_tab(void) {
    two t;
    two_new(&t);
    t.app.tab = MT_TAB_INFER;
    RHAS(&t.app, 160, 48, "target sin(pi x)", "dgx-00", "gx10-9959", "points", "64");
    two_free(&t);
}

static void http_tab_input_and_response(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    app->tab = MT_TAB_HTTP;
    KK(app, TUI_KEY_BACKSPACE);
    for (const char *c = "/stats"; *c; c++) CHECK(is_none(keyc(app, (uint32_t)*c)));
    CHECK_S(app->http_input, "/stats");
    CHECK(sent_is(keyk(app, TUI_KEY_ENTER), 0, "http /stats"));
    const char *body = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nhello from the gpu\n";
    apply(app, mt_update_reply_bytes(0, "http /stats", body, strlen(body)), mt_now());
    RHAS(app, 160, 48, "HTTP/1.0 200 OK", "hello from the gpu", "GET /hello HTTP/1.0", "curl -s http://127.0.0.1:8088/stats", "served by GPU 3");
    two_free(&t);
}

static void global_keys(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    CHECK(is_none(keyc(app, ']')));
    CHECK_U(app->focus, 1);
    /* doom runs on machine 1 -> d stops it; X resets train; R no longer reloads. */
    CHECK(sent_is(keyc(app, 'd'), 1, "stop doom"));
    CHECK(sent_is(keyc(app, 'X'), 1, "reset train"));
    CHECK(is_none(keyc(app, 'R')));
    tui_key ctrl_c = tui_key_char('c');
    ctrl_c.mods = TUI_MOD_CONTROL;
    mt_action a = key_ev(app, ctrl_c);
    CHECK(a.kind == MT_ACTION_QUIT);
    mt_action_free(&a);
    app->tab = MT_TAB_HTTP;
    a = key_ev(app, ctrl_c);
    CHECK(a.kind == MT_ACTION_QUIT);
    mt_action_free(&a);
    app->tab = MT_TAB_TRAIN;
    a = keyc(app, 'q');
    CHECK(a.kind == MT_ACTION_QUIT);
    mt_action_free(&a);
    CHECK(is_none(keyc(app, '6')));
    CHECK_U(app->tab, MT_TAB_HTTP);
    two_free(&t);
}

static void loss_history_accumulates_and_resets(void) {
    const char *ep = "a:1";
    mt_app app;
    mt_app_init(&app, &ep, 1);
    mt_mock_world w;
    world(&w, "dgx-00", MESH_PEER_NONE);
    const uint32_t steps[3] = {200, 400, 600};
    for (int i = 0; i < 3; i++) {
        w.frozen_step = steps[i];
        apply(&app, mt_update_snap(0, mt_mock_synth(&w)), mt_now());
    }
    const mt_machine *m = &app.machines[0];
    CHECK_U(m->nlosses, 600);
    if (m->nlosses == 600) {
        CHECK_U(m->losses[0].step, 0);
        CHECK_U(m->losses[599].step, 599);
    }
    bool ordered = true;
    for (size_t i = 1; i < m->nlosses; i++) ordered &= m->losses[i - 1].step < m->losses[i].step;
    CHECK(ordered);
    w.frozen_step = 40; /* reset train */
    apply(&app, mt_update_snap(0, mt_mock_synth(&w)), mt_now());
    CHECK_U(app.machines[0].nlosses, 40);
    mt_app_free(&app);
    mt_mock_world_free(&w);
}

static void counter_rates_follow_snapshots_and_reset_on_upgrade(void) {
    const char *ep = "a:1";
    mt_app app;
    mt_app_init(&app, &ep, 1);
    mt_mock_world w;
    world(&w, "dgx-00", MESH_PEER_NONE);
    mt_instant t0 = mt_now();
    apply(&app, mt_update_snap(0, mt_mock_synth(&w)), t0);
    w.started -= MT_SECS(1); /* one more second of doom: 120 tasks = 30 frames */
    apply(&app, mt_update_snap(0, mt_mock_synth(&w)), t0 + MT_SECS(1));
    float r = mt_machine_counter_rate(&app.machines[0], 0, 0);
    CHECK(r >= 27.0f && r < 33.0f);
    /* Another image in the slot (loads changed): its rate starts over. */
    w.progs[0].loads += 1;
    apply(&app, mt_update_snap(0, mt_mock_synth(&w)), t0 + MT_SECS(2));
    CHECK(mt_machine_counter_rate(&app.machines[0], 0, 0) == 0.0f);
    mt_app_free(&app);
    mt_mock_world_free(&w);
}

static void tiny_panes_do_not_panic(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    const uint16_t hs[6] = {0, 1, 3, 5, 18, 30};
    for (int tab = 0; tab < MT_TABS; tab++) {
        app->tab = tab;
        for (uint16_t w = 0; w < 14; w++)
            for (int h = 0; h < 6; h++) free(render(app, w, hs[h]));
    }
    /* The prompts too. */
    app->tab = MT_TAB_PROGRAMS;
    app->prompt.kind = MT_PROMPT_LOAD;
    snprintf(app->prompt.input, sizeof app->prompt.input, "x");
    app->prompt.has_pick = false;
    const uint16_t ps[3][2] = {{1, 1}, {10, 4}, {30, 6}};
    for (int i = 0; i < 3; i++) free(render(app, ps[i][0], ps[i][1]));
    app->prompt.kind = MT_PROMPT_UNLOAD;
    app->prompt.machine = 0;
    app->prompt.name = strdup("doom");
    free(render(app, 5, 3));
    two_free(&t);
}

static void flame_ages_survive_globaltimer_wrap(void) {
    /* http's 128 records span ~64 s (2 tasks/s): the low words wrap ~15 times. */
    two t;
    two_new(&t);
    const mt_machine *m = &t.app.machines[0];
    mt_flame_rec *recs;
    size_t n = mt_machine_flame(m, mt_now(), 100000000, &recs);
    bool young = true;
    size_t doom = 0, http = 0;
    for (size_t i = 0; i < n; i++) {
        young &= recs[i].age < 100000000;
        doom += recs[i].rec.slot == 0;
        http += recs[i].rec.slot == 3;
    }
    CHECK(young);
    /* doom: 4 tasks every ~33 ms -> 3 or 4 batches in the window, on all 4 nodes. */
    CHECK(doom >= 12 && doom <= 16);
    /* http: only its newest task is within 100 ms. */
    CHECK_U(http, 1);
    free(recs);
    /* A stale trace (count unmoved for > 0.5 s) leaves the chart. */
    CHECK_U(mt_machine_flame(m, mt_now() + MT_SECS(1), 100000000, &recs), 0);
    free(recs);
    two_free(&t);
}

/* The middle row of the Doom frame: how many different (fg, bg) pairs. */
static size_t frame_variety(const tui_buffer *b, uint16_t y, uint16_t x0, uint16_t x1) {
    tui_color seen[256][2];
    size_t n = 0;
    for (uint16_t x = x0; x < x1; x++) {
        const tui_cell *c = tui_buffer_cell_c(b, x, y);
        if (strcmp(tui_cell_symbol(c), "▀")) continue;
        bool dup = false;
        for (size_t i = 0; i < n && !dup; i++) dup = tui_color_eq(seen[i][0], c->fg) && tui_color_eq(seen[i][1], c->bg);
        if (!dup && n < 256) {
            seen[n][0] = c->fg;
            seen[n][1] = c->bg;
            n++;
        }
    }
    return n;
}

/* The marker row: the row with the most "━" (first of equal maxima is not what max_by_key keeps:
 * the last is). */
static uint16_t marker_row(const tui_buffer *b, uint16_t w, uint16_t h) {
    uint16_t best = 0;
    size_t bn = 0;
    for (uint16_t y = 0; y < h; y++) {
        size_t n = 0;
        for (uint16_t x = 0; x < w; x++) n += !strcmp(tui_cell_symbol(tui_buffer_cell_c(b, x, y)), "━");
        if (n >= bn) {
            bn = n;
            best = y;
        }
    }
    return best;
}

static void doom_tab_each_placement(void) {
    mt_instant now = mt_now();
    struct {
        uint32_t mode, sys;
        const char *side[2], *title[2], *label;
    } cases[4] = {
        {MESH_PLACE_LOCAL, 0, {"place local", "place local"}, {"", ""}, "local"},
        {MESH_PLACE_PIN, 0, {"runs here", "mirrored here"}, {"pinned to system 0", "pinned to system 0"}, "pin 0"},
        {MESH_PLACE_PIN, 1, {"mirrored here", "runs here"}, {"pinned to system 1", "pinned to system 1"}, "pin 1"},
        {MESH_PLACE_SPLIT, 0, {"owner (pose)", "follower"},
         {"split: cols 0-63 system 0 · 64-127 system 1", "split: cols 0-63 system 0 · 64-127 system 1"}, "split"},
    };
    for (int ci = 0; ci < 4; ci++) {
        two t;
        world(&t.ws[0], "dgx-00", 0);
        world(&t.ws[1], "gx10-9959", 1);
        for (int i = 0; i < 2; i++) {
            t.ws[i].place[0] = cases[ci].mode;
            t.ws[i].place[1] = cases[ci].sys;
        }
        app_with(&t.app, t.ws, 2);
        mt_app *app = &t.app;
        app->tab = MT_TAB_DOOM;
        for (size_t focus = 0; focus < 2; focus++) {
            app->focus = focus;
            tui_buffer buf;
            char *s = render_buf(app, 160, 48, now, &buf);
            HAS(s, cases[ci].label, cases[ci].side[focus], cases[ci].title[focus]);
            /* Walls are drawn from the shown frame even where doom's own arrays are stale. */
            CHECK(frame_variety(&buf, 24, 2, 120) >= 5);
            CHECK(!strstr(s, "press Esc, then d"));
            uint16_t row = marker_row(&buf, 160, 48);
            size_t c0 = 0, c1 = 0, cells = 0;
            uint16_t a = 0, b = 0;
            for (uint16_t x = 0; x < 160; x++) {
                const tui_cell *c = tui_buffer_cell_c(&buf, x, row);
                if (strcmp(tui_cell_symbol(c), "━")) continue;
                if (!cells) a = x;
                b = x;
                cells++;
                c0 += color_is(c->fg, 90, 210, 255);
                c1 += color_is(c->fg, 255, 150, 80);
            }
            bool span = cells >= 20;
            if (!span) c0 = c1 = 0;
            if (cases[ci].mode == MESH_PLACE_LOCAL) {
                CHECK(!span);
            } else if (cases[ci].mode == MESH_PLACE_PIN && cases[ci].sys == 0) {
                CHECK(c0 > 100 && c1 == 0);
            } else if (cases[ci].mode == MESH_PLACE_PIN) {
                CHECK(c1 > 100 && c0 == 0);
            } else {
                /* Split: system 0's colour on the left half, system 1's on the right. */
                CHECK(c0 > 50 && c1 > 50);
                CHECK(span && color_is(tui_buffer_cell_c(&buf, a, row)->fg, 90, 210, 255) && color_is(tui_buffer_cell_c(&buf, b, row)->fg, 255, 150, 80));
                HAS(s, "split", "added ", "1.25 ms");
            }
            if (cases[ci].mode != MESH_PLACE_LOCAL) {
                uint32_t to = cases[ci].mode == MESH_PLACE_PIN ? cases[ci].sys : 0;
                char want[64];
                if (to == focus) snprintf(want, sizeof want, "keys  here");
                else snprintf(want, sizeof want, "keys  → system %u", to);
                HAS(s, cases[ci].label, want);
            }
            free(s);
            tui_buffer_free(&buf);
        }
        /* The Programs tab shows the placement on both machines. */
        app->tab = MT_TAB_PROGRAMS;
        char *s = render_buf(app, 160, 48, now, NULL);
        CHECK(strstr(s, "place") && count_of(s, cases[ci].label) >= 2);
        free(s);
        two_free(&t);
    }
}

static void doom_placement_keys(void) {
    two t;
    two_new(&t);
    mt_app *app = &t.app;
    app->tab = MT_TAB_DOOM;
    /* p cycles from local; v toggles split/single; either machine asks. */
    CHECK(sent_is(keyc(app, 'p'), 0, "place doom pin 0"));
    CHECK(sent_is(keyc(app, 'v'), 0, "place doom split"));
    app->focus = 1;
    CHECK(sent_is(keyc(app, 'v'), 1, "place doom split"));
    mt_app_free(app);
    struct {
        uint32_t mode, sys;
        const char *p, *v;
    } cases[3] = {{MESH_PLACE_PIN, 0, "place doom pin 1", "place doom split"},
                  {MESH_PLACE_PIN, 1, "place doom split", "place doom split"},
                  {MESH_PLACE_SPLIT, 0, "place doom local", "place doom local"}};
    for (int ci = 0; ci < 3; ci++) {
        for (int i = 0; i < 2; i++) {
            t.ws[i].place[0] = cases[ci].mode;
            t.ws[i].place[1] = cases[ci].sys;
        }
        app_with(app, t.ws, 2);
        app->tab = MT_TAB_DOOM;
        CHECK(sent_is(keyc(app, 'p'), 0, cases[ci].p));
        CHECK(sent_is(keyc(app, 'v'), 0, cases[ci].v));
        /* Movement keys still go to the focused machine's meshd (it forwards them). */
        CHECK(sent_is(keyc(app, 'w'), 0, "input doom w"));
        mt_app_free(app);
    }
    mt_mock_world_free(&t.ws[0]);
    mt_mock_world_free(&t.ws[1]);
    /* Without doom: nothing is sent. */
    mt_mock_world w;
    world(&w, "dgx-00", 0);
    size_t k = 0;
    for (size_t i = 0; i < w.nprogs; i++)
        if (strcmp(w.progs[i].name, "doom")) w.progs[k++] = w.progs[i];
    w.nprogs = k;
    mt_app a1;
    app_with(&a1, &w, 1);
    a1.tab = MT_TAB_DOOM;
    CHECK(is_none(keyc(&a1, 'p')));
    CHECK(strstr(a1.last_reply, "doom is not loaded") != NULL);
    mt_app_free(&a1);
    mt_mock_world_free(&w);
}

static void programs_tab_native_worker(void) {
    mt_mock_world w;
    world(&w, "mac", 0);
    mt_mock_catalog_prog("gemm", 5, &w.progs[w.nprogs++]);
    mt_app app;
    app_with(&app, &w, 1);
    app.tab = MT_TAB_PROGRAMS;
    char *s = render(&app, 170, 48);
    char *row = line_with(s, "gemm", "periodic");
    CHECK(row != NULL);
    if (row) HAS(row, "gemm row", "native", "40005", "98", "17.0M", "2/1");
    free(row);
    row = line_with(s, "doom", "periodic");
    CHECK(row && strstr(row, "cuda")); /* GPU programs show their backend */
    free(row);
    HAS(s, "worker totals", "workers 1, wasm lanes 0: 98% CPU, 17.0M");
    free(s);
    /* Narrow: the kind column stays. */
    s = render(&app, 100, 30);
    row = line_with(s, "gemm", "native");
    CHECK(row != NULL);
    free(row);
    free(s);
    mt_app_free(&app);
    mt_mock_world_free(&w);
}

int main(void) {
    mt_clock_virtual(true, T0);
    mesh_tab();
    mesh_offline_panel();
    programs_tab_table_view_flame_log();
    programs_tab_generic_view_for_unknown_program();
    programs_keys();
    load_prompt_lists_the_catalog();
    programs_tab_polls_trace_and_log_on_focus();
    doom_tab_draws_walls_and_sends_input();
    custom_tabs_say_not_loaded();
    train_tab_shows_identity();
    infer_tab();
    http_tab_input_and_response();
    global_keys();
    loss_history_accumulates_and_resets();
    counter_rates_follow_snapshots_and_reset_on_upgrade();
    tiny_panes_do_not_panic();
    flame_ages_survive_globaltimer_wrap();
    doom_tab_each_placement();
    doom_placement_keys();
    programs_tab_native_worker();
    return mesh_check_done();
}
