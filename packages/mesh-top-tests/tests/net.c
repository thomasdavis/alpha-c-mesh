/* crates/mesh-top tests/net.rs and load.rs, ported: the snapshot's link, NIC and port counters,
 * rates from consecutive snapshots on meshd's clock (with restarts), the load telemetry
 * accessors, and the network and load views at their sizes, from synthetic snapshots. */
#include "support.h"

#include <math.h>

static const char *const HOSTS[4] = {"dgx-00", "gx10-9959", "being", "monumentals-mac-studio.local"};

static void worlds(mt_mock_world *ws, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        mt_mock_world_init(&ws[k], HOSTS[k], k);
        ws[k].n = n;
        ws[k].has_frozen_step = true;
        ws[k].frozen_step = 4000;
    }
}

static void free_worlds(mt_mock_world *ws, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) mt_mock_world_free(&ws[k]);
}

/* Every machine fed one snapshot per meshd time in times (ms). */
static void feed(mt_app *app, mt_mock_world *ws, size_t n, const uint64_t *times, size_t nt) {
    mt_instant t0 = mt_now();
    for (size_t q = 0; q < nt; q++)
        for (size_t i = 0; i < n; i++) {
            ws[i].has_clock_ms = true;
            ws[i].clock_ms = times[q];
            apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0);
        }
}

static void app_of(mt_app *app, mt_mock_world *ws, size_t n, const uint64_t *times, size_t nt) {
    mk_app(app, n, 7700, 10);
    feed(app, ws, n, times, nt);
}

static bool near(double a, double b) { return fabs(a - b) <= 1e-6 * fmax(fabs(b), 1.0); }

static void snapshot_network_fields_at_abi_offsets(void) {
    mt_mock_world ws[4];
    worlds(ws, 4);
    ws[0].has_clock_ms = true;
    ws[0].clock_ms = 2500;
    mt_snapshot s = mt_mock_synth(&ws[0]);
    uint64_t rounds = (uint64_t)MT_MOCK_ROUNDS_PER_S * 2500 / 1000;
    mt_link_stat l1 = mt_snapshot_link(&s, 1);
    CHECK_U(l1.tx_frames[MESH_KIND_ROUND], rounds);
    CHECK_U(l1.rx_bytes, rounds * MT_MOCK_ROUND_BYTES + 10);
    CHECK_S(l1.iface, "enp1s0f0np0");
    CHECK_S(l1.local, "192.168.177.11:40001");
    CHECK_S(mt_snapshot_link(&s, 2).iface, "enP7s7");
    mt_link_stat l0 = mt_snapshot_link(&s, 0);
    bool empty = l0.rx_bytes == 0 && l0.tx_bytes == 0 && !l0.local[0] && !l0.iface[0];
    for (int k = 0; k < MESH_NET_KINDS; k++) empty &= l0.rx_frames[k] == 0 && l0.tx_frames[k] == 0;
    CHECK(empty); /* no link to itself */
    CHECK_U(mt_snapshot_uptime_ms(&s), 2500);
    uint64_t at;
    uint32_t samples;
    mt_snapshot_net_at(&s, &at, &samples);
    CHECK(at == 2000 && samples == 2);
    mt_nic nics[MESH_MAX_NICS];
    size_t nn = mt_snapshot_nics(&s, nics);
    CHECK_U(nn, 2);
    CHECK(nn == 2 && !strcmp(nics[0].name, "enP7s7") && nics[0].links == 12 && nics[0].count == 1);
    CHECK(nn == 2 && !strcmp(nics[1].name, "enp1s0f0np0") && nics[1].links == 2 && nics[1].count == 1);
    mt_nic t = mt_snapshot_net_total(&s);
    CHECK(!strcmp(t.name, "total") && t.count == 2);
    CHECK_U(t.v[0], nics[0].v[0] + nics[1].v[0] + 50 * 2000);
    uint64_t ctl[3], http[3];
    mt_snapshot_ports(&s, ctl, http);
    CHECK(ctl[0] == 5 * 37 && ctl[1] == 3000 * 37 && ctl[2] == 37);
    CHECK_U(http[2], 0);
    /* The accessors read the ABI offsets: patch one field in the raw bytes and read it back. */
    size_t host = s.len - MESH_SNAP_HOST;
    uint8_t *b = malloc(s.len);
    memcpy(b, s.bytes, s.len);
    mt_put_u64(b, host + mesh_link(3) + MESH_L_TX_FRAMES + 8 * MESH_KIND_LOAD, 77);
    mt_put_u64(b, host + mesh_nic(1) + MESH_NI_RX_ERRS, 5);
    mt_snapshot p;
    char *err;
    CHECK(reparse(b, s.len, &p, &err));
    CHECK_U(mt_snapshot_link(&p, 3).tx_frames[MESH_KIND_LOAD], 77);
    mt_snapshot_nics(&p, nics);
    CHECK_U(nics[1].v[4], 5);
    mt_snapshot_free(&p);
    /* A record count past MAX_NICS is clamped, never read out of the region. */
    memcpy(b, s.bytes, s.len);
    mt_put_u32(b, host + MESH_S_NET_NICS, 999);
    CHECK(reparse(b, s.len, &p, &err));
    CHECK_U(mt_snapshot_nics(&p, nics), MESH_MAX_NICS);
    mt_snapshot_free(&p);
    free(b);
    mt_snapshot_free(&s);
    free_worlds(ws, 4);
}

static void rates_from_consecutive_snapshots(void) {
    mt_mock_world ws[4];
    worlds(ws, 4);
    mt_app app;
    const uint64_t times[7] = {1000, 1250, 1500, 1750, 2000, 2500, 3000};
    app_of(&app, ws, 4, times, 7);
    const mt_machine *m = &app.machines[0];
    const mt_link_track *l = mt_net_link(&m->net, 2);
    CHECK(l != NULL);
    if (l) {
        /* Rounds: 2000/s each way; bytes: 2000 * 77 + 4 pings of 1 byte. */
        double want = (double)(MT_MOCK_ROUNDS_PER_S * MT_MOCK_ROUND_BYTES + 4);
        CHECK(near(mt_meter_per_s(&l->tx), want) && near(mt_meter_per_s(&l->rx), want));
        double rx, tx;
        mt_link_frames_per_s(l, MESH_KIND_ROUND, &rx, &tx);
        CHECK(near(tx, 2000.0));
        mt_link_frames_per_s(l, MESH_KIND_CTL, &rx, &tx);
        CHECK(near(rx, 4.0));
        mt_link_frames_per_s(l, MESH_KIND_PLACE, &rx, &tx);
        CHECK(rx == 0.0 && tx == 0.0);
        CHECK(near(mt_net_mesh_bytes(&m->net), 3.0 * 2.0 * want));
        /* NICs: rates between their samples (1000, 2000, 3000), on the sample clock. */
        const mt_nic_track *lan = mt_net_nic(&m->net, "enP7s7");
        CHECK(lan && near(mt_nic_rx(lan), 2.0 * 2.0 * want + 50000.0));
        double total = 2.0 * 3.0 * want + 100000.0;
        CHECK(near(mt_nic_rx(&m->net.total), total) && near(mt_nic_tx(&m->net.total), total));
        double share;
        CHECK(mt_net_mesh_share(&m->net, &share) && near(share, 6.0 * want / (2.0 * total)));
        CHECK_U(m->net.nhist, 2); /* samples 2000 and 3000 (1000 alone has no rate) */
        CHECK(near(mt_meter_per_s(&m->net.ctl[2]), 15.0) && near(mt_meter_per_s(&m->net.ctl[1]), 45000.0));
        uint64_t age;
        CHECK(mt_net_nic_age_ms(&m->net, &age) && age == 0);
    }
    mt_app_free(&app);
    free_worlds(ws, 4);
}

static void split_placement_shows_as_place_frames(void) {
    mt_mock_world ws[4];
    worlds(ws, 4);
    for (int i = 0; i < 4; i++) {
        ws[i].place[0] = MESH_PLACE_SPLIT;
        ws[i].place[1] = 0;
    }
    mt_app app;
    const uint64_t times[3] = {1000, 2000, 3000};
    app_of(&app, ws, 4, times, 3);
    const mt_link_track *l = mt_net_link(&app.machines[3].net, 0);
    double rx = 0, tx = 0;
    if (l) mt_link_frames_per_s(l, MESH_KIND_PLACE, &rx, &tx);
    CHECK(l && near(rx, 30.0));
    mt_app_free(&app);
    free_worlds(ws, 4);
}

static void a_restart_starts_the_rates_over(void) {
    mt_mock_world ws[2];
    worlds(ws, 2);
    mt_app app;
    const uint64_t t1[3] = {5000, 6000, 7000}, t2[1] = {300}, t3[3] = {1000, 2000, 3000};
    app_of(&app, ws, 2, t1, 3);
    CHECK(mt_meter_per_s(&mt_net_link(&app.machines[0].net, 1)->tx) > 0.0);
    /* meshd restarted: its uptime and every counter start again near 0. */
    feed(&app, ws, 2, t2, 1);
    const mt_machine *m = &app.machines[0];
    CHECK(mt_meter_per_s(&mt_net_link(&m->net, 1)->tx) == 0.0);
    CHECK(mt_nic_rx(&m->net.total) == 0.0);
    CHECK_U(m->net.nhist, 0);
    feed(&app, ws, 2, t3, 3);
    CHECK(near(mt_meter_per_s(&mt_net_link(&m->net, 1)->tx), (double)(MT_MOCK_ROUNDS_PER_S * MT_MOCK_ROUND_BYTES + 4)));
    CHECK(mt_nic_rx(&m->net.total) > 0.0);
    mt_app_free(&app);
    free_worlds(ws, 2);
}

static void views_at_220x55(void) {
    mt_mock_world ws[4];
    worlds(ws, 4);
    mt_app app;
    const uint64_t times[3] = {1000, 2000, 3000};
    app_of(&app, ws, 4, times, 3);
    RHAS(&app, 220, 55, "net rx", "mesh", "Network", "→1", "→3", "enp1s0f0np0", "enP7s7", "total(2)", "ctl", "r2.0k/2.0k", "1.0 MB/s", "45%");
    app.tab = MT_TAB_NET;
    char *s = render(&app, 220, 55);
    HAS(s, "net 220x55", "link matrix", "0 dgx-00", "3 monumentals", "links", "interfaces", "total (2)", "enp1s0f0np0", "ctl / http", "2.0k", "15 req");
    /* Every directed link: 12 of them in a cluster of 4. */
    CHECK_U(count_of(s, " → "), 12);
    free(s);
    mt_app_free(&app);
    free_worlds(ws, 4);
}

static void views_at_100x30(void) {
    mt_mock_world ws[4];
    worlds(ws, 4);
    mt_app app;
    const uint64_t times[3] = {1000, 2000, 3000};
    app_of(&app, ws, 4, times, 3);
    /* One panel (the focused machine); its Network box in the short form. */
    RHAS(&app, 100, 30, "Network", "→1", "→2", "→3", "mesh 45%");
    app.tab = MT_TAB_NET;
    RHAS(&app, 100, 30, "link matrix", "links");
    mt_app_free(&app);
    free_worlds(ws, 4);
    /* A single machine (no cluster): only the interfaces table. */
    mt_mock_world one;
    mt_mock_world_init(&one, "solo", MESH_PEER_NONE);
    app_of(&app, &one, 1, times, 2);
    app.tab = MT_TAB_NET;
    char *s = render(&app, 100, 30);
    CHECK(strstr(s, "interfaces") && !strstr(s, "link matrix"));
    free(s);
    mt_app_free(&app);
    mt_mock_world_free(&one);
}

static void tiny_network_panes_do_not_panic(void) {
    mt_mock_world ws[4];
    worlds(ws, 4);
    mt_app app;
    const uint64_t times[3] = {1000, 2000, 3000};
    app_of(&app, ws, 4, times, 3);
    const int tabs[2] = {MT_TAB_MESH, MT_TAB_NET};
    const uint16_t wsz[7] = {0, 1, 7, 20, 40, 60, 99}, hsz[8] = {0, 1, 2, 4, 9, 14, 21, 30};
    for (int t = 0; t < 2; t++) {
        app.tab = tabs[t];
        for (int i = 0; i < 7; i++)
            for (int j = 0; j < 8; j++) free(render(&app, wsz[i], hsz[j]));
    }
    mt_app_free(&app);
    free_worlds(ws, 4);
}

/* ---- load.rs */

static const char *const LHOSTS[3] = {"dgx-00", "gx10-9959", "being"};

static void lworlds(mt_mock_world *ws, uint32_t n) {
    for (uint32_t k = 0; k < n; k++) {
        mt_instant t = mt_now() - MT_SECS(20);
        mt_mock_world_init(&ws[k], LHOSTS[k], k);
        ws[k].n = n;
        ws[k].step0 = t;
        ws[k].started = t;
        ws[k].has_frozen_step = true;
        ws[k].frozen_step = 4000;
    }
}

static void lapp(mt_app *app, mt_mock_world *ws, size_t n) {
    mk_app(app, n, 57900, 1);
    mt_instant t0 = mt_now();
    for (size_t i = 0; i < n; i++) {
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0);
        apply(app, mt_update_snap(i, mt_mock_synth(&ws[i])), t0 + MT_MS(66));
    }
}

static void snapshot_accessors(void) {
    mt_mock_world ws[2];
    lworlds(ws, 2);
    mt_snapshot s = mt_mock_synth(&ws[0]);
    mt_load l;
    CHECK(mt_snapshot_load(&s, &l));
    CHECK(l.share.some && l.share.v == 0.48f && l.cpus == 20 && l.cpus_big == 10 && l.cpus_little == 10);
    CHECK_U(l.ncores, 20);
    CHECK(l.cores[9][0] == 100 && l.cores[9][1] == MESH_CORE_BIG);
    CHECK(l.has_psi && l.psi[0] == 0.0016f);
    CHECK(!l.has_mem_level && !l.has_thermal && !l.util_ext.some);
    CHECK(l.temp_c.some && l.age_ms <= 400);
    mt_prog_load p;
    CHECK(mt_snapshot_prog_load(&s, 0, &p));
    CHECK(p.share10.some && p.share10.v == 0.48f && p.p50_us.some && p.p50_us.v == 812.0f && p.task_us.some && p.task_us.v == 830.0f &&
          p.lat_n == 1280);
    CHECK(!mt_snapshot_prog_load(&s, 20, &p));
    mt_member_load ml;
    CHECK(mt_snapshot_member_load(&s, 1, &ml) && ml.share.some && ml.share.v == 0.58f && ml.cpus_big == 10 && ml.cpus_little == 10);
    CHECK(!mt_snapshot_member_load(&s, 5, &ml));
    /* Flags gate the values: GPU not observed -> none, not 0. */
    size_t host = s.len - MESH_SNAP_HOST;
    uint8_t *b = malloc(s.len);
    memcpy(b, s.bytes, s.len);
    mt_put_u32(b, host + MESH_S_LOAD_FLAGS, MESH_LF_PSI);
    mt_put_u32(b, host + mesh_pload(0) + MESH_PL_FLAGS, MESH_PLF_LOADED);
    mt_snapshot s2;
    char *err;
    CHECK(reparse(b, s.len, &s2, &err));
    CHECK(mt_snapshot_load(&s2, &l) && !l.share.some && !l.inflate.some);
    CHECK(mt_snapshot_prog_load(&s2, 0, &p) && !p.share.some && !p.p99_us.some && !p.task_us.some);
    mt_snapshot_free(&s2);
    /* No sample yet: none. */
    memcpy(b, s.bytes, s.len);
    memset(b + host + MESH_S_LOAD_AT_MS, 0, 8);
    CHECK(reparse(b, s.len, &s2, &err));
    CHECK(!mt_snapshot_load(&s2, &l));
    mt_snapshot_free(&s2);
    /* snapshot_v2_is_refused */
    memcpy(b, s.bytes, s.len);
    mt_put_u32(b, MESH_SN_VERSION, 2);
    CHECK(!reparse(b, s.len, &s2, &err) && err && strstr(err, "version 2"));
    free(err);
    free(b);
    mt_snapshot_free(&s);
    free_worlds(ws, 2);
}

static void overview_load_columns(void) {
    mt_mock_world ws[3];
    lworlds(ws, 3);
    mt_app app;
    lapp(&app, ws, 3);
    /* Full: GPU got / CPU B/L / PSI / meshd. */
    RHAS(&app, 240, 50, "GPU got", "CPU B/L", "PSI c/m", "meshd", "48%", "58%", "68%", "11/2", "0.2/0.0%");
    /* 140..230: share and CPU in one column, meshd beside it; the wait column still fits. */
    RHAS(&app, 200, 50, "GPU CPU", "48% 7%", "2:72us");
    /* 100: one column; 80: none, the hosts keep their room. */
    RHAS(&app, 100, 30, "GPU CPU", "58% 7%");
    char *s = render(&app, 80, 24);
    CHECK(!strstr(s, "GPU CPU") && strstr(s, "gx10-9959"));
    free(s);
    mt_app_free(&app);
    free_worlds(ws, 3);
}

static void overview_member_without_endpoint_shows_its_loadrep(void) {
    mt_mock_world ws[3];
    lworlds(ws, 3);
    mt_app app;
    lapp(&app, ws, 2);
    char *s = render(&app, 240, 50);
    /* System 2 (mock-2) is known only from the members' records: its F_LOADREP share 68%. */
    char *row = line_with(s, "mock-2", NULL);
    CHECK(row && strstr(row, "68%") && strstr(row, "via peers"));
    free(row);
    free(s);
    mt_app_free(&app);
    free_worlds(ws, 3);
}

static void machine_panel_load_box(void) {
    mt_mock_world ws[2];
    lworlds(ws, 2);
    mt_app app;
    lapp(&app, ws, 2);
    RHAS(&app, 160, 60, "Load", "GPU got 48%", "slice 2.0ms", "max 2.3ms", "×2.08", "big 11%", "little 2%", "PSI cpu 0.2%", "meshd 1%",
         "disk 192G free");
    /* Shorter: the compact line, then none; never a panic. */
    char *s = render(&app, 160, 40);
    CHECK(strstr(s, "GPU 48%") || strstr(s, "GPU got 48%"));
    free(s);
    const uint16_t hs[4] = {30, 24, 12, 6};
    for (int i = 0; i < 4; i++) free(render(&app, 100, hs[i]));
    mt_app_free(&app);
    free_worlds(ws, 2);
}

static void load_tab(void) {
    mt_mock_world ws[2];
    lworlds(ws, 2);
    mt_app app;
    lapp(&app, ws, 2);
    CHECK(is_none(keyc(&app, '8')));
    CHECK_U(app.tab, MT_TAB_LOAD);
    RHAS(&app, 200, 50, "8 Load", "GPU got", "dgx-00", "gx10-9959", "48%", "58%", "2.0ms", "2.3ms", "big     10", "little  10", "doom", "train",
         "812us", "1.5ms", "2.4ms", "830us", "120.0", "0.2/0.0/0.0%", "86/122G", "69°C", "GPU got, % per second");
    RHAS(&app, 100, 30, "8 Load", "GPU got", "dgx-00", "48%", "doom");
    const uint16_t hs[6] = {0, 1, 2, 4, 8, 12};
    for (uint16_t w = 0; w < 20; w++)
        for (int h = 0; h < 6; h++) free(render(&app, w, hs[h]));
    mt_app_free(&app);
    free_worlds(ws, 2);
    /* An endpoint with no snapshot yet. */
    const char *ep = "127.0.0.1:1";
    mt_app_init(&app, &ep, 1);
    app.tab = MT_TAB_LOAD;
    RHAS(&app, 120, 30, "connect");
    mt_app_free(&app);
}

int main(void) {
    mt_clock_virtual(true, T0);
    snapshot_network_fields_at_abi_offsets();
    rates_from_consecutive_snapshots();
    split_placement_shows_as_place_frames();
    a_restart_starts_the_rates_over();
    views_at_220x55();
    views_at_100x30();
    tiny_network_panes_do_not_panic();
    snapshot_accessors();
    overview_load_columns();
    overview_member_without_endpoint_shows_its_loadrep();
    machine_panel_load_box();
    load_tab();
    return mesh_check_done();
}
