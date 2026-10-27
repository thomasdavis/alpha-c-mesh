/* crates/mesh-top tests/views.rs, ported: the Fleet (9), Jobs (0) and Pipes (-) tabs drawn from
 * meshd's records, their keys (each operation asks to confirm), the polling, and the sizes. The
 * records are views.rs's FLEET, JOBS and PIPES, in tests/data/golden_*.txt. */
#include "support.h"

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
    *len = (size_t)n;
    return b;
}

static char *fixture(const char *view, size_t *len) {
    char path[64];
    snprintf(path, sizeof path, "tests/data/golden_%s.txt", view);
    char *t = slurp(path, len);
    if (!t) abort();
    return t;
}

static void app_new(mt_app *app) {
    mk_app(app, 4, 64700, 1);
    const char *const views[3] = {"fleet", "jobs", "pipes"};
    for (int i = 0; i < 3; i++) {
        size_t len;
        char *t = fixture(views[i], &len);
        char line[32];
        snprintf(line, sizeof line, "view %s", views[i]);
        apply(app, mt_update_reply_bytes(1, line, t, len), mt_now());
        free(t);
    }
}

static void records_parse(void) {
    size_t len;
    char *t = fixture("fleet", &len);
    mt_vrec *r;
    size_t n = mt_views_parse(t, &r);
    uint64_t u;
    uint32_t list[8];
    CHECK(n > 0 && !strcmp(r[0].kind, "fleet") && mt_vrec_u(&r[0], "leader", &u) && u == 1);
    CHECK(n > 0 && !strcmp(mt_vrec_get(&r[0], "suspect"), "3") && mt_vrec_list(&r[0], "cordon", list, 8) == 1 && list[0] == 2);
    const mt_vrec *m3 = NULL;
    size_t cells = 0;
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(r[i].kind, "member") && mt_vrec_u(&r[i], "id", &u) && u == 3) m3 = &r[i];
        cells += !strcmp(r[i].kind, "cell");
    }
    CHECK(m3 && mt_vrec_flag(m3, "busy") && !mt_vrec_get(m3, "share") && !strcmp(m3->text, "busy: cpu 0.91 for 6 s"));
    CHECK_U(cells, 20);
    mt_vrecs_free(r, n);
    free(t);
    mt_view v = mt_view_new(0, mt_now(), "err view \"x\": fleet, jobs or pipes [json]\n");
    CHECK(v.n == 0 && v.err && !strcmp(v.err, "view \"x\": fleet, jobs or pipes [json]"));
    mt_view_free(&v);
}

static void fleet_tab(void) {
    mt_app app;
    app_new(&app);
    KC(&app, '9');
    CHECK_U(app.tab, MT_TAB_FLEET);
    RHAS(&app, 100, 30, "leader 1", "term 7", "version 42", "suspect 3", "scheduler running", "cordon 2", "busy 3", "migrations 4", "2.4 ms", "members",
         "dgx-00", "gx10-9959", "being", "mac-studio", "suspect B", "up      L", "up      C", "pending", "(41)", "busy: cpu", "placement", "http",
         "pipesink-w", "tick-r", "train", "●C", "●Wr", "<", "○C>", "●Cp", "●M", "why", "decisions", "migrate tick from system 0 to 2", "proposal",
         "re-forming group train", "lockstep groups", "epoch 4", "reform after 5000 ms", "paused@3");
    /* The explain follows the selection: tick (row 3), then the Mac's verdict. */
    KK(&app, TUI_KEY_DOWN);
    KK(&app, TUI_KEY_DOWN);
    RHAS(&app, 100, 30, "tick at 2", "chosen", "1.998 = perf", "rejected busy (owner policy)");
    /* Wider: the caps columns; smaller: panes drop, the headline survives; tiny: no panic. */
    RHAS(&app, 200, 50, "cpus", "mem GB", "10/10", "94/122", "the gain 41 %");
    RHAS(&app, 80, 24, "leader 1", "members", "placement");
    RHAS(&app, 60, 16, "leader 1", "dgx-00");
    RHAS(&app, 39, 8, "leader 1");
    free(render(&app, 12, 3));
    free(render(&app, 1, 1));
    mt_app_free(&app);
}

static void fleet_keys_ask_to_confirm(void) {
    mt_app app;
    app_new(&app);
    KC(&app, '9');
    /* The cursor: program 2 (tick), member 2 (being, cordoned). */
    KK(&app, TUI_KEY_DOWN);
    KK(&app, TUI_KEY_DOWN);
    KK(&app, TUI_KEY_RIGHT);
    KK(&app, TUI_KEY_RIGHT);
    char *prog;
    bool has_m;
    uint32_t m;
    mt_app_fleet_sel(&app, &prog, &has_m, &m);
    CHECK(prog && !strcmp(prog, "tick") && has_m && m == 2);
    free(prog);
    const struct {
        char c;
        const char *line;
    } cases[5] = {{'c', "sched uncordon 2"}, {'p', "sched pin tick 2"}, {'u', "sched unpin tick"}, {'z', "sched pause"}, {'g', "group train reform"}};
    for (int i = 0; i < 5; i++) {
        CHECK(is_none(keyc(&app, (uint32_t)cases[i].c)));
        CHECK(app.prompt.kind == MT_PROMPT_CONFIRM && app.prompt.line && !strcmp(app.prompt.line, cases[i].line));
        RHAS(&app, 100, 30, "confirm", cases[i].line, "y confirm");
        CHECK(sent_is(keyc(&app, 'y'), 1, cases[i].line));
        CHECK(app.prompt.kind == MT_PROMPT_NONE);
    }
    /* Any other key cancels; the cordon of an uncordoned member. */
    KK(&app, TUI_KEY_LEFT);
    KC(&app, 'c');
    CHECK(app.prompt.kind == MT_PROMPT_CONFIRM && !strcmp(app.prompt.line, "sched cordon 1"));
    CHECK(is_none(keyc(&app, 'n')));
    CHECK(app.prompt.kind == MT_PROMPT_NONE && strstr(app.last_reply, "cancelled"));
    mt_app_free(&app);
}

static void jobs_tab(void) {
    mt_app app;
    app_new(&app);
    KC(&app, '0');
    CHECK_U(app.tab, MT_TAB_JOBS);
    RHAS(&app, 100, 30, "jobs 3", "active 2", "leader 1", "37.9k", "1.1", "hashjob", "running", "17%", "500909 / 3000000", "20.7k", "2m01s", "1.2",
         "sha256d", "endless", "1.3", "done", "100%", "job 1.1", "share", "28%", "140608", "1655", "6655.6", "lease choice", "eligible", "2.002 = perf",
         "rejected", "cordoned", "busy (owner policy", "item 502604 = 289358284", "failed 77", "R_JOB_FAIL");
    /* Select the endless job, cancel it (confirmed). */
    KK(&app, TUI_KEY_DOWN);
    char *sel = mt_app_job_sel(&app);
    CHECK(sel && !strcmp(sel, "1.2"));
    free(sel);
    KC(&app, 'x');
    RHAS(&app, 100, 30, "cancel job 1.2", "job cancel 1.2");
    CHECK(sent_is(keyc(&app, 'y'), 1, "job cancel 1.2"));
    RHAS(&app, 200, 50, "prio", "job 1.2");
    RHAS(&app, 70, 20, "jobs 3", "hashjob");
    RHAS(&app, 39, 6, "jobs 3");
    free(render(&app, 5, 2));
    mt_app_free(&app);
}

static void pipes_tab(void) {
    mt_app app;
    app_new(&app);
    KC(&app, '-');
    CHECK_U(app.tab, MT_TAB_PIPES);
    RHAS(&app, 100, 30, "inboxes 2", "refusing now (backpressure) 1", "pipehash", "msg", "1020/1024", "8800.0", "1210.5", "189515", "pipesrc",
         "pipesink-w", "digest", "4/4096", "feeds", "pipesrc → pipehash,pipesink-w");
    RHAS(&app, 200, 50, "empty", "64 B");
    RHAS(&app, 39, 6, "inboxes 2");
    free(render(&app, 3, 1));
    mt_app_free(&app);
}

static bool one_poll(mt_app *app, mt_instant t, size_t machine, const char *line) {
    mt_action a[2];
    size_t n = mt_app_tick(app, t, a);
    bool ok = n == 1 && a[0].kind == MT_ACTION_POLL && a[0].machine == machine && !strcmp(a[0].line, line);
    for (size_t i = 0; i < n; i++) mt_action_free(&a[i]);
    return ok;
}

static size_t polls(mt_app *app, mt_instant t) {
    mt_action a[2];
    size_t n = mt_app_tick(app, t, a);
    for (size_t i = 0; i < n; i++) mt_action_free(&a[i]);
    return n;
}

static void views_are_polled_on_their_tabs(void) {
    const char *eps[2] = {"a:1", "b:2"};
    mt_app app;
    mt_app_init(&app, eps, 2);
    mt_instant t0 = mt_now();
    CHECK_U(polls(&app, t0), 0);
    KC(&app, '9');
    /* No snapshot names a leader: the focused machine is asked. */
    CHECK(one_poll(&app, t0, 0, "view fleet"));
    CHECK_U(polls(&app, t0 + MT_MS(500)), 0);
    CHECK(one_poll(&app, t0 + MT_VIEW_POLL, 0, "view fleet"));
    KC(&app, ']');
    KC(&app, '-');
    CHECK(one_poll(&app, t0 + MT_VIEW_POLL, 1, "view pipes"));
    /* Before the first answer: what is asked of whom, never a blank tab. */
    RHAS(&app, 100, 30, "asking", "view pipes");
    /* An error is shown where the view would be. */
    apply(&app, mt_update_reply_text(1, "view pipes", "err the control port is busy"), t0);
    RHAS(&app, 100, 30, "the control port is busy");
    mt_app_free(&app);
}

static void the_tab_bar_fits(void) {
    mt_app app;
    app_new(&app);
    KC(&app, '0');
    RHAS(&app, 140, 10, "1 Mesh", "9 Fleet", "0 Jobs", "- Pipes");
    /* 100 columns: the current tab keeps its name, the others their keys. */
    char *s = render(&app, 100, 10);
    char *top = strndup(s, (size_t)(strchr(s, '\n') - s));
    CHECK(strstr(top, "0 Jobs") && strstr(top, " 9 ") && !strstr(top, "Fleet"));
    free(top);
    free(s);
    s = render(&app, 40, 10);
    top = strndup(s, (size_t)(strchr(s, '\n') - s));
    CHECK(strstr(top, "0 Jobs") != NULL);
    free(top);
    free(s);
    mt_app_free(&app);
}

int main(void) {
    mt_clock_virtual(true, T0);
    records_parse();
    fleet_tab();
    fleet_keys_ask_to_confirm();
    jobs_tab();
    pipes_tab();
    views_are_polled_on_their_tabs();
    the_tab_bar_fits();
    return mesh_check_done();
}
