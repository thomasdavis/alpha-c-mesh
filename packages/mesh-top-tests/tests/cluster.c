/* crates/mesh-top tests/cluster.rs and fleet.rs, ported: N-machine clusters (the overview, focus
 * cycling, the N-way identity check, doom's per-part system colours) and the fleet region of the
 * snapshot with the Fleet tab drawn from it. */
#include "support.h"

static const char *const HOSTS[4] = {"dgx-00", "gx10-9959", "being", "spare-3"};

/* Systems 0..n, the third a slower x86 RTX 3060 (heterogeneous). */
static void worlds(mt_mock_world *ws, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        mt_instant t = mt_now() - MT_SECS(20);
        mt_mock_world *w = &ws[k];
        mt_mock_world_init(w, HOSTS[k], k);
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

static void free_worlds(mt_mock_world *ws, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) mt_mock_world_free(&ws[k]);
}

static void app_with(mt_app *app, mt_mock_world *ws, size_t n) {
    mk_app(app, n, 27700, 1);
    mt_instant t0 = mt_now();
    for (size_t i = 0; i < n; i++) {
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0);
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0 + MT_MS(66));
    }
}

static void overview_lists_every_machine(void) {
    for (uint32_t n = 3; n <= 4; n++) {
        mt_mock_world ws[4];
        worlds(ws, n);
        mt_app app;
        app_with(&app, ws, n);
        CHECK_U(mt_app_cluster_size(&app), n);
        const uint16_t sz[2][2] = {{200, 50}, {80, 24}};
        for (int i = 0; i < 2; i++) {
            char *s = render(&app, sz[i][0], sz[i][1]);
            char systems[32], linked[16];
            snprintf(systems, sizeof systems, "%u systems", n);
            snprintf(linked, sizeof linked, "%u/%u", n, n);
            HAS(s, "overview", "cluster", systems, "RTX 3060", "8.6", "12.1", linked);
            for (uint32_t k = 0; k < n; k++) HAS(s, "overview hosts", HOSTS[k]);
            free(s);
        }
        /* Wide: arch and blocks per SM, and each machine's wait per member. */
        RHAS(&app, 200, 50, "x86_64", "aarch64", "blk/SM", "2:72us");
        mt_app_free(&app);
        free_worlds(ws, n);
    }
}

static void overview_fills_a_member_without_endpoint(void) {
    /* A 3-system cluster watched through two endpoints: system 2 from the members' records. */
    mt_mock_world ws[3];
    worlds(ws, 3);
    mt_app app;
    app_with(&app, ws, 2);
    RHAS(&app, 200, 50, "via peers", "mock-2");
    mt_app_free(&app);
    free_worlds(ws, 3);
}

static void absent_member_is_flagged(void) {
    mt_mock_world ws[3];
    worlds(ws, 3);
    for (int i = 0; i < 3; i++) {
        ws[i].down[0] = 2;
        ws[i].ndown = 1;
    }
    mt_app app;
    app_with(&app, ws, 2);
    RHAS(&app, 200, 50, "2:absent", "2/3");
    mt_app_free(&app);
    free_worlds(ws, 3);
}

static void focus_cycles_over_every_endpoint(void) {
    mt_mock_world ws[3];
    worlds(ws, 3);
    mt_app app;
    app_with(&app, ws, 3);
    size_t seen[3];
    for (int i = 0; i < 3; i++) {
        KC(&app, ']');
        seen[i] = app.focus;
    }
    CHECK(seen[0] == 1 && seen[1] == 2 && seen[2] == 0);
    KC(&app, '[');
    CHECK_U(app.focus, 2);
    /* On the Doom tab too, and the title follows the focus. */
    app.tab = MT_TAB_DOOM;
    KC(&app, '[');
    CHECK_U(app.focus, 1);
    RHAS(&app, 200, 50, "focus [ gx10-9959 ]");
    mt_app_free(&app);
    free_worlds(ws, 3);
}

static void identity_across_three(void) {
    mt_mock_world ws[3];
    worlds(ws, 3);
    mt_app app;
    app_with(&app, ws, 3);
    app.tab = MT_TAB_TRAIN;
    mt_identity id;
    CHECK(mt_params_identity(&app, &id) && id.step == 4000 && id.n == 3 && id.identical);
    RHAS(&app, 200, 50, "train params bitwise identical across 3: YES (at step 4000)");
    /* System 2's params differ at the same step: NO, and it is named. */
    mt_snapshot s = flipped(&ws[2], MESH_S_LOCK_VIEW + 3, 0x80);
    app.machines[2].nparams = 0;
    apply(&app, mt_update_snap(2, s), mt_now());
    CHECK(mt_params_identity(&app, &id) && !id.identical && id.ndiffer == 1 && !strcmp(id.differ[0], "system 2"));
    RHAS(&app, 200, 50, "across 3: NO (at step 4000)", "differ: system 2");
    /* Only the steps every machine reported count: one without the step means waiting. */
    app.machines[1].nparams = 1;
    app.machines[1].params[0].step = 9999;
    memset(app.machines[1].params[0].view, 0, 32);
    CHECK(!mt_params_identity(&app, &id));
    mt_app_free(&app);
    free_worlds(ws, 3);
}

/* The marker row: per cell, the colour of the system that rendered that column (the row with
 * the most "━", the last of equal maxima). */
static size_t marker_row(const tui_buffer *b, uint16_t w, uint16_t h, tui_color *out) {
    uint16_t row = 0;
    size_t bn = 0;
    for (uint16_t y = 0; y < h; y++) {
        size_t n = 0;
        for (uint16_t x = 0; x < w; x++) n += !strcmp(tui_cell_symbol(tui_buffer_cell_c(b, x, y)), "━");
        if (n >= bn) {
            bn = n;
            row = y;
        }
    }
    size_t n = 0;
    for (uint16_t x = 0; x < w; x++) {
        const tui_cell *c = tui_buffer_cell_c(b, x, row);
        if (!strcmp(tui_cell_symbol(c), "━")) out[n++] = c->fg;
    }
    return n;
}

static size_t count_color(const tui_color *row, size_t n, tui_color c) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) k += tui_color_eq(row[i], c);
    return k;
}

static void doom_split_over_three_colours_each_part(void) {
    mt_mock_world ws[3];
    worlds(ws, 3);
    for (int i = 0; i < 3; i++) {
        ws[i].place[0] = MESH_PLACE_SPLIT;
        ws[i].place[1] = 0;
    }
    mt_app app;
    app_with(&app, ws, 3);
    app.tab = MT_TAB_DOOM;
    const char *roles[3] = {"owner (pose), 2 parts", "follower, 1 part", "follower, 1 part"};
    for (size_t focus = 0; focus < 3; focus++) {
        app.focus = focus;
        tui_buffer buf;
        char *s = render_buf(&app, 200, 50, mt_now(), &buf);
        HAS(s, "split", "split: cols 0-63 system 0 · 64-95 system 1 · 96-127 system 2", roles[focus], "parts 0 0 1 2",
            focus == 0 ? "keys  here" : "keys  → system 0");
        tui_color row[200];
        size_t n = marker_row(&buf, 200, 50, row);
        tui_color a = MT_MACHINE[0], b = MT_MACHINE[1], c = MT_MACHINE[2];
        size_t ca = count_color(row, n, a), cb = count_color(row, n, b), cc = count_color(row, n, c);
        CHECK(ca > cb && cb > 10 && cc > 10);
        /* Left half system 0, then a quarter of system 1, the last quarter system 2. */
        CHECK(n > 0 && tui_color_eq(row[0], a) && tui_color_eq(row[n / 2 + n / 8], b) && tui_color_eq(row[n - 1], c));
        free(s);
        tui_buffer_free(&buf);
    }
    mt_app_free(&app);
    free_worlds(ws, 3);
}

static void place_key_cycles_every_pin(void) {
    mt_mock_world ws[3];
    worlds(ws, 3);
    const uint32_t places[5][2] = {{MESH_PLACE_LOCAL, 0}, {MESH_PLACE_PIN, 0}, {MESH_PLACE_PIN, 1}, {MESH_PLACE_PIN, 2}, {MESH_PLACE_SPLIT, 0}};
    const char *want[5] = {"place doom pin 0", "place doom pin 1", "place doom pin 2", "place doom split", "place doom local"};
    mt_app app;
    for (int p = 0; p < 5; p++) {
        for (int i = 0; i < 3; i++) {
            ws[i].place[0] = places[p][0];
            ws[i].place[1] = places[p][1];
        }
        app_with(&app, ws, 3);
        app.tab = MT_TAB_DOOM;
        mt_action a = keyc(&app, 'p');
        CHECK(a.kind == MT_ACTION_SEND && a.line && !strcmp(a.line, want[p]));
        mt_action_free(&a);
        mt_app_free(&app);
    }
    /* Pinned to system 2: system 2's colour across the whole marker row, mirrored on system 0. */
    for (int i = 0; i < 3; i++) {
        ws[i].place[0] = MESH_PLACE_PIN;
        ws[i].place[1] = 2;
    }
    app_with(&app, ws, 3);
    app.tab = MT_TAB_DOOM;
    tui_buffer buf;
    char *s = render_buf(&app, 200, 50, mt_now(), &buf);
    HAS(s, "pin 2", "mirrored here", "keys  → system 2");
    tui_color row[200];
    size_t n = marker_row(&buf, 200, 50, row);
    CHECK(n > 100 && count_color(row, n, MT_MACHINE[2]) == n);
    free(s);
    tui_buffer_free(&buf);
    mt_app_free(&app);
    free_worlds(ws, 3);
}

/* ---- fleet.rs */

static const char *const FHOSTS[4] = {"dgx-00", "gx10-9959", "being", "mac-studio"};

/* Member k of 4 as it sees the fleet: system 1 leads term 7, version 12; system 3 is gone (and
 * failed its last reconcile); 2 is suspect from k's side. */
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
        mt_put_u32(b, fo + MESH_FM_STATUS, j == 3 ? MESH_RS_FAILED : j == 2 ? MESH_RS_PENDING : MESH_RS_SYNC);
        mt_put_u32(b, fo + MESH_FM_GROUP, j == 3 ? MESH_GL_NONE : MESH_GL_PAUSED);
        mt_put_u64(b, fo + MESH_FM_APPLIED, j == 3 ? 9 : j == 2 ? 11 : 12);
        mt_put_u64(b, fo + MESH_FM_HEARD_MS, j == 3 ? 0 : 1000);
        mt_put_str(b, fo + MESH_FM_REASON, 40, j == 3 ? "load http: unsupported" : j == 2 ? "reconciling version 12" : "ok");
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
    char *err;
    if (!reparse(s.bytes, s.len, &p, &err)) abort();
    mt_snapshot_free(&s);
    return p;
}

static void fleet_accessor(void) {
    mt_snapshot s = fleet_snap(0);
    mt_fleet f;
    CHECK(mt_snapshot_fleet(&s, &f));
    CHECK(f.has_leader && f.leader == 1 && f.role == MESH_ROLE_FOLLOWER && f.term == 7 && f.version == 12 && f.applied == 12);
    CHECK(f.up == 7 && f.suspect == 4 && f.quorum == 3);
    CHECK_U(f.nmembers, 4);
    CHECK(f.members[3].status == MESH_RS_FAILED && !strcmp(f.members[3].reason, "load http: unsupported") && f.members[2].applied == 11);
    CHECK(f.has_group);
    const mt_fleet_group *g = &f.group;
    CHECK(!strcmp(g->name, "train") && g->epoch == 4 && g->cycle == 2 && g->mask == 15 && !g->running && g->local == MESH_GL_PAUSED && g->reform &&
          g->auto_ms == 5000);
    mt_jobs j = mt_snapshot_jobs(&s);
    CHECK(j.active == 2 && j.total == 3 && j.done == 12345 && j.flags == 0);
    /* No leader: none, not system 0xffffffff. */
    mt_put_u32(s.bytes, s.len - MESH_SNAP_HOST + MESH_S_LEADER, MESH_PEER_NONE);
    mt_snapshot p;
    char *err;
    CHECK(reparse(s.bytes, s.len, &p, &err));
    CHECK(mt_snapshot_fleet(&p, &f) && !f.has_leader);
    mt_snapshot_free(&p);
    mt_snapshot_free(&s);
}

static void fleet_tab(void) {
    mt_app app;
    mk_app(&app, 4, 57900, 1);
    mt_instant t0 = mt_now();
    for (uint32_t k = 0; k < 3; k++) {
        apply(&app, mt_update_snap(k, fleet_snap(k)), t0);
        apply(&app, mt_update_snap(k, fleet_snap(k)), t0 + MT_MS(66));
    }
    CHECK(is_none(keyc(&app, '9')));
    CHECK_U(app.tab, MT_TAB_FLEET);
    char *s = render(&app, 200, 50);
    HAS(s, "fleet", "9 Fleet", "each member's view", "follower", "leader", "members as dgx-00 sees them", "suspect", "absent", "in-sync", "pending",
        "failed", "load http: unsupported", "train", "epoch", "reform after 5000 ms", "paused", "jobs: ", "2 active of 3", "12345");
    /* The machine without a snapshot is listed with its status. */
    CHECK(strstr(s, "mac-studio") || strstr(s, "57903"));
    free(s);
    /* Narrower and tiny: no panic, the headline survives. */
    RHAS(&app, 100, 30, "fleet", "members");
    const uint16_t sz[3][2] = {{60, 12}, {30, 6}, {10, 3}};
    for (int i = 0; i < 3; i++) free(render(&app, sz[i][0], sz[i][1]));
    mt_app_free(&app);
}

int main(void) {
    mt_clock_virtual(true, T0);
    overview_lists_every_machine();
    overview_fills_a_member_without_endpoint();
    absent_member_is_flagged();
    focus_cycles_over_every_endpoint();
    identity_across_three();
    doom_split_over_three_colours_each_part();
    place_key_cycles_every_pin();
    fleet_accessor();
    fleet_tab();
    return mesh_check_done();
}
