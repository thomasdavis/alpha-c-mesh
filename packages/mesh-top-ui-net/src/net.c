/* ui.rs's "network" and "7 net" sections, ported. */
#include "mesh/top_ui_net.h"

#include <string.h>

static const char *human(mt_frame *f, double x) {
    char b[64];
    mt_human(x, b, sizeof b);
    return tui_arena_strdup(f->a, b);
}

static const char *sh(mt_frame *f, double x) {
    char b[64];
    mt_short(x, b, sizeof b);
    return tui_arena_strdup(f->a, b);
}

static const char *spark(mt_frame *f, const mt_net_track *n, size_t w) {
    char *o = tui_arena_alloc(f->a, 3 * w + 1);
    mt_spark(n->hist, n->nhist, w, o);
    return o;
}

static const char *share_text(mt_frame *f, const mt_net_track *n) {
    double x;
    return mt_net_mesh_share(n, &x) ? MT_FMT(f, "%s%%", mt_ff(f, x * 100.0, 0)) : "-";
}

static tui_span dim_if_zero(double x, const char *s) { return mt_fgs(s, x > 0.0 ? MT_FG : MT_DIM); }

size_t mt_net_cells(mt_frame *f, const mt_machine *m, bool wide, tui_table_cell *out) {
    const mt_net_track *n = &m->net;
    double rx = mt_nic_rx(&n->total), tx = mt_nic_tx(&n->total);
    double x;
    tui_span share = mt_net_mesh_share(n, &x) ? mt_fgs(MT_FMT(f, "%s%%", mt_ff(f, x * 100.0, 0)), MT_ACCENT) : mt_fgs("-", MT_FAINT);
    size_t k = 0;
    uint64_t age;
    if (!mt_net_nic_age_ms(n, &age)) {
        out[k++] = mt_cell_span(f, mt_fgs("no sample", MT_DIM));
        out[k++] = mt_cell_str(f, "");
    } else if (wide) {
        out[k++] = mt_cell_span(f, dim_if_zero(rx, human(f, rx)));
        out[k++] = mt_cell_span(f, dim_if_zero(tx, human(f, tx)));
    } else {
        out[k++] = mt_cell_str(f, MT_FMT(f, "%s %s", sh(f, rx), sh(f, tx)));
    }
    out[k++] = mt_cell_span(f, share);
    if (wide) out[k++] = mt_cell_span(f, mt_fgs(spark(f, n, MT_OV_SPARK), MT_ACCENT));
    return k;
}

/* A member's short name for the network rows (its JOIN hostname). */
static const char *peer_name(mt_frame *f, const mt_snapshot *s, uint32_t k, size_t w) {
    mt_member mb = mt_snapshot_member(s, k);
    const char *h = *mb.host ? mb.host : MT_FMT(f, "system %u", k);
    return mt_take(f, h, w);
}

/* `2.0k/1.9k` (out/in frames/s), `-` when both are 0. */
static const char *frames_pair(mt_frame *f, double out, double inn) {
    if (out == 0.0 && inn == 0.0) return "-";
    return MT_FMT(f, "%s/%s", sh(f, out), sh(f, inn));
}

/* The part of an address before its last ':' (the whole when none). */
static const char *addr_of(mt_frame *f, const char *local) {
    const char *c = strrchr(local, ':');
    return c ? tui_arena_strndup(f->a, local, (size_t)(c - local)) : tui_arena_strdup(f->a, local);
}

static tui_style bad_style(double x) { return tui_fg(x > 0.0 ? MT_BAD : MT_DIM); }

static tui_line nic_line(mt_frame *f, bool wide, const char *name, const mt_nic_track *t, const tui_span *extra, size_t nextra) {
    double rx = mt_nic_rx(t), tx = mt_nic_tx(t), pr, pt;
    mt_nic_packets(t, &pr, &pt);
    double e = mt_nic_errors(t), d = mt_nic_drops(t);
    tui_line v;
    if (wide)
        v = MT_LINE(f, mt_fgs(mt_pad(f, name, 50, '<'), MT_FG),
                    mt_fgb(MT_FMT(f, "%s%s", mt_pad(f, human(f, rx), 12, '>'), mt_pad(f, human(f, tx), 12, '>')), MT_FG),
                    mt_fgs(MT_FMT(f, "  %s", mt_pad(f, MT_FMT(f, "%s/%s", sh(f, pr), sh(f, pt)), 10, '>')), MT_DIM),
                    mt_styled(MT_FMT(f, " pkt/s  err %s/s", sh(f, e)), bad_style(e)),
                    mt_styled(MT_FMT(f, " drop %s/s ", sh(f, d)), bad_style(d)));
    else
        v = MT_LINE(f, mt_fgs(mt_pad(f, mt_take(f, name, 11), 12, '<'), MT_FG),
                    mt_fgb(MT_FMT(f, "↓%s↑%s", mt_pad(f, sh(f, rx), 6, '<'), mt_pad(f, sh(f, tx), 6, '<')), MT_FG),
                    mt_fgs(MT_FMT(f, "%s/%sp ", sh(f, pr), sh(f, pt)), MT_DIM), mt_styled(MT_FMT(f, "e%s ", sh(f, e)), bad_style(e)),
                    mt_styled(MT_FMT(f, "d%s ", sh(f, d)), bad_style(d)));
    for (size_t i = 0; i < nextra; i++) tui_line_push(f->a, &v, extra[i]);
    return v;
}

void mt_net_lines(mt_frame *f, const mt_machine *m, const mt_snapshot *s, uint16_t width, bool compact, mt_lines *out) {
    size_t w = width;
    bool wide = w >= 100 && !compact;
    const mt_net_track *n = &m->net;
    uint32_t me;
    bool has_me = mt_snapshot_system(s, &me);
    if (wide && has_me) {
        const char *h = MT_FMT(f, "%s%s%s%s  %s%s%s%s%s", mt_pad(f, "link", 20, '<'), mt_pad(f, "interface  local address", 30, '<'),
                               mt_pad(f, "rx", 12, '>'), mt_pad(f, "tx", 12, '>'), mt_pad(f, "round o/i", 10, '>'),
                               mt_pad(f, "place o/i", 10, '>'), mt_pad(f, "load", 8, '>'), mt_pad(f, "ctl", 8, '>'), mt_pad(f, "RTT", 8, '>'));
        mt_lines_push(f, out, tui_line_span(f->a, mt_label(h)));
    }
    if (has_me) {
        uint32_t ns = mt_snapshot_systems(s);
        for (uint32_t k = 0; k < ns; k++) {
            if (k == me) continue;
            mt_member mb = mt_snapshot_member(s, k);
            mt_link_stat l = mt_snapshot_link(s, k);
            const mt_link_track *t = mt_net_link(n, k);
            if (!t) continue;
            double rx = mt_meter_per_s(&t->rx), tx = mt_meter_per_s(&t->tx);
            double fr[MESH_NET_KINDS][2];
            for (size_t i = 0; i < MESH_NET_KINDS; i++) {
                double r, x;
                mt_link_frames_per_s(t, i, &r, &x);
                fr[i][0] = x;
                fr[i][1] = r;
            }
            bool up = mb.state == MESH_MS_UP;
            tui_style name_st = tui_fg_bold(up ? mt_sys_color(k) : MT_BAD);
            const char *rtt = up && mb.rtt_us > 0.0f ? mt_fmt_us(f, mb.rtt_us) : "-";
            const char *iface = *l.iface ? l.iface : "-";
            const char *addr = addr_of(f, l.local);
            const char *arrow = MT_FMT(f, "→%u ", k);
            if (wide) {
                mt_lines_push(f, out,
                              MT_LINE(f, mt_styled(arrow, name_st), mt_fgs(mt_pad(f, peer_name(f, s, k, 16), 17, '<'), MT_FG),
                                      mt_fgs(mt_pad(f, mt_take(f, MT_FMT(f, "%s %s", iface, addr), 29), 30, '<'), MT_DIM),
                                      mt_fgb(MT_FMT(f, "%s%s", mt_pad(f, human(f, rx), 12, '>'), mt_pad(f, human(f, tx), 12, '>')), MT_FG),
                                      mt_fgs(MT_FMT(f, "  %s%s", mt_pad(f, frames_pair(f, fr[0][0], fr[0][1]), 10, '>'),
                                                    mt_pad(f, frames_pair(f, fr[1][0], fr[1][1]), 10, '>')),
                                             MT_WARN),
                                      mt_fgs(MT_FMT(f, "%s%s", mt_pad(f, frames_pair(f, fr[2][0], fr[2][1]), 8, '>'),
                                                    mt_pad(f, frames_pair(f, fr[3][0], fr[3][1]), 8, '>')),
                                             MT_DIM),
                                      mt_fgs(mt_pad(f, rtt, 8, '>'), MT_ACCENT)));
            } else if (compact) {
                mt_lines_push(f, out,
                              MT_LINE(f, mt_styled(arrow, name_st), mt_fgs(mt_pad(f, peer_name(f, s, k, 8), 9, '<'), MT_FG),
                                      mt_fgb(MT_FMT(f, "↓%s↑%s", mt_pad(f, sh(f, rx), 6, '<'), mt_pad(f, sh(f, tx), 6, '<')), MT_FG),
                                      mt_fgs(MT_FMT(f, "r%s", mt_pad(f, sh(f, fr[0][0]), 6, '<')), MT_WARN), mt_fgs(rtt, MT_ACCENT)));
            } else {
                mt_lines_push(f, out,
                              MT_LINE(f, mt_styled(arrow, name_st), mt_fgs(mt_pad(f, peer_name(f, s, k, 15), 16, '<'), MT_FG),
                                      mt_fgs(MT_FMT(f, "%s %s", iface, addr), MT_DIM)));
                tui_line l2 = MT_LINE(f, tui_span_raw("   "),
                                      mt_fgb(MT_FMT(f, "↓%s↑%s", mt_pad(f, sh(f, rx), 6, '<'), mt_pad(f, sh(f, tx), 6, '<')), MT_FG),
                                      mt_fgs(MT_FMT(f, "r%s ", frames_pair(f, fr[0][0], fr[0][1])), MT_WARN));
                static const char tag[4] = {0, 'p', 'l', 'c'};
                for (size_t i = 1; i < 4; i++)
                    tui_line_push(f->a, &l2, mt_fgs(MT_FMT(f, "%c%s ", tag[i], frames_pair(f, fr[i][0], fr[i][1])), MT_DIM));
                tui_line_push(f->a, &l2, mt_fgs(rtt, MT_ACCENT));
                mt_lines_push(f, out, l2);
            }
        }
    }
    /* The NICs that carry links, then the total. */
    if (!compact) {
        mt_nic nics[MESH_MAX_NICS];
        size_t nn = mt_snapshot_nics(s, nics);
        for (size_t i = 0; i < nn; i++) {
            const mt_nic *x = &nics[i];
            const char *name = wide ? MT_FMT(f, "%s (to %s)", x->name, mt_mask_list(f, x->links)) : tui_arena_strdup(f->a, x->name);
            const mt_nic_track *t = mt_net_nic(n, x->name);
            if (t && x->count > 0)
                mt_lines_push(f, out, nic_line(f, wide, name, t, NULL, 0));
            else
                mt_lines_push(f, out, MT_LINE(f, mt_fgs(MT_FMT(f, "%s ", x->name), MT_FG), mt_fgs("gone", MT_BAD)));
        }
    }
    mt_nic total = mt_snapshot_net_total(s);
    const char *share = share_text(f, n);
    uint64_t age;
    if (!mt_net_nic_age_ms(n, &age)) {
        mt_lines_push(f, out, tui_line_span(f->a, mt_fgs("NICs: no sample yet", MT_DIM)));
    } else {
        const char *name = wide ? MT_FMT(f, "total (%u interfaces)", total.count) : MT_FMT(f, "total(%u)", total.count);
        /* Narrow: the mesh share is in the box's title. */
        tui_span extra[2];
        size_t ne = 0;
        if (wide) extra[ne++] = mt_fgs(MT_FMT(f, "mesh %s", share), MT_ACCENT);
        if (age > 3000) extra[ne++] = mt_fgb(MT_FMT(f, " sample %llus old", (unsigned long long)(age / 1000)), MT_BAD);
        mt_lines_push(f, out, nic_line(f, wide, name, &n->total, extra, ne));
    }
    if (!compact) {
        const mt_meter *c = n->ctl, *h = n->http;
        mt_lines_push(f, out,
                      MT_LINE(f, mt_label("ctl "),
                              mt_fgs(MT_FMT(f, "↓%s ↑%s %s/s", sh(f, mt_meter_per_s(&c[0])), sh(f, mt_meter_per_s(&c[1])), sh(f, mt_meter_per_s(&c[2]))), MT_FG),
                              mt_label(wide ? " requests   http relay " : "  http "),
                              mt_fgs(MT_FMT(f, "↓%s ↑%s %s/s", sh(f, mt_meter_per_s(&h[0])), sh(f, mt_meter_per_s(&h[1])), sh(f, mt_meter_per_s(&h[2]))), MT_FG),
                              mt_label(wide ? " connections" : "")));
    }
}

void mt_draw_net_box(mt_frame *f, tui_rect area, const mt_machine *m, const mt_lines *lines) {
    if (area.height == 0 || area.width == 0) return;
    const mt_net_track *n = &m->net;
    const char *share = share_text(f, n);
    tui_line title = MT_LINE(f, mt_fgb(" Network ", MT_FG),
                             mt_label(MT_FMT(f, "↓%s ↑%s B/s ", sh(f, mt_nic_rx(&n->total)), sh(f, mt_nic_tx(&n->total)))),
                             mt_fgs(MT_FMT(f, "mesh %s ", share), MT_ACCENT));
    if (area.width >= 60) tui_line_push(f->a, &title, mt_label("frames/s out/in "));
    tui_block b = tui_block_bordered();
    b.set = &TUI_BORDER_ROUNDED;
    b.border_style = tui_fg(MT_FAINT);
    tui_block_add_title(f->a, &b, title, TUI_TITLE_DEFAULT);
    tui_rect inner = tui_block_inner(&b, area);
    mt_block(f, &b, area);
    mt_par(f, mt_text(f, lines), inner);
}

/* System k's hostname from its own snapshot (13 characters at most). */
static const char *sys_host(mt_frame *f, const mt_machine *const *by_sys, uint32_t k) {
    return by_sys[k] ? mt_take(f, mt_name(f, by_sys[k]), 13) : "";
}

typedef struct {
    bool has_tx, has_rx;
    double tx, rx;
} dir_rates;

/* Directed link k -> j: (sender's tx, receiver's rx), bytes/s. */
static dir_rates dir(const mt_machine *const *by_sys, uint32_t k, uint32_t j) {
    dir_rates d = {0};
    const mt_link_track *t;
    if (by_sys[k] && (t = mt_net_link(&by_sys[k]->net, j))) {
        d.has_tx = true;
        d.tx = mt_meter_per_s(&t->tx);
    }
    if (by_sys[j] && (t = mt_net_link(&by_sys[j]->net, k))) {
        d.has_rx = true;
        d.rx = mt_meter_per_s(&t->rx);
    }
    return d;
}

static void table_in(mt_frame *f, const mt_rows *rows, tui_table_row *hdr, const tui_constraint *w, size_t nw, tui_line title, tui_rect area) {
    tui_block b = mt_panel(f, title, MT_FAINT);
    tui_table t = tui_table_new(rows->rows, rows->n, w, nw);
    t.header = hdr;
    t.column_spacing = 1;
    t.block = &b;
    mt_table(f, &t, area);
}

void mt_draw_net(mt_frame *f, tui_rect area, const mt_app *app) {
    uint32_t n = mt_app_cluster_size(app);
    const mt_machine **by_sys = tui_arena_alloc(f->a, (n ? n : 1) * sizeof *by_sys);
    for (uint32_t k = 0; k < n; k++)
        for (size_t i = 0; i < app->nmachines; i++) {
            uint32_t sk;
            if (mt_machine_system(&app->machines[i], &sk) && sk == k) {
                by_sys[k] = &app->machines[i];
                break;
            }
        }
    bool clustered = false;
    for (size_t i = 0; i < app->nmachines; i++) {
        uint32_t sk;
        clustered |= mt_machine_system(&app->machines[i], &sk);
    }
    uint16_t links_h = clustered ? (uint16_t)(n * (n ? n - 1 : 0) + 3) : 0;
    uint16_t matrix_h = clustered ? (uint16_t)(n + 4) : 0;
    size_t nic_rows = 0;
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_machine *m = &app->machines[i];
        if (m->has_snap) {
            mt_nic tmp[MESH_MAX_NICS];
            nic_rows += mt_snapshot_nics(&m->snap, tmp) + 2;
        } else {
            nic_rows += 1;
        }
    }
    tui_rect r[3];
    tui_constraint cons[3] = {TUI_LENGTH(matrix_h), TUI_LENGTH(links_h), TUI_MIN(nic_rows < 3 ? nic_rows : 3)};
    tui_vsplit(area, 3, cons, r);
    tui_rect mx = r[0], links = r[1], nics = r[2];
    if (clustered && mx.height > 0) {
        tui_table_row header = tui_table_row_new();
        tui_table_row_push(f->a, &header, mt_cell_span(f, mt_label("from \\ to")));
        for (uint32_t j = 0; j < n; j++)
            tui_table_row_push(f->a, &header, mt_cell_span(f, mt_fgb(MT_FMT(f, "%u %s", j, sys_host(f, by_sys, j)), mt_sys_color(j))));
        tui_table_row_push(f->a, &header, mt_cell_span(f, mt_label("sent, all links")));
        mt_rows rows = {0};
        for (uint32_t k = 0; k < n; k++) {
            tui_table_row row = tui_table_row_new();
            tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgb(MT_FMT(f, "%u %s", k, sys_host(f, by_sys, k)), mt_sys_color(k))));
            double sum = 0.0;
            for (uint32_t j = 0; j < n; j++) {
                if (j == k) {
                    tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgs("·", MT_FAINT)));
                    continue;
                }
                dir_rates d = dir(by_sys, k, j);
                sum += d.has_tx ? d.tx : 0.0;
                const char *t = d.has_tx ? sh(f, d.tx) : "?";
                const char *rr = d.has_rx ? sh(f, d.rx) : "?";
                tui_table_row_push(f->a, &row,
                                   mt_cell_line(f, MT_LINE(f, mt_fgb(mt_pad(f, t, 6, '>'), MT_FG), mt_fgs(MT_FMT(f, " / %s", mt_pad(f, rr, 6, '<')), MT_DIM))));
            }
            tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgs(human(f, sum), MT_ACCENT)));
            mt_rows_push(f, &rows, row);
        }
        tui_constraint *w = tui_arena_alloc(f->a, (n + 2) * sizeof *w);
        w[0] = TUI_LENGTH(16);
        for (uint32_t j = 0; j < n; j++) w[1 + j] = TUI_LENGTH(16);
        w[n + 1] = TUI_FILL(1);
        tui_line title = MT_LINE(f, mt_fgb(" link matrix ", MT_FG),
                                 mt_label("bytes/s: sent (the sender's count) / received (the receiver's count) "));
        table_in(f, &rows, &header, w, n + 2, title, mx);
    }
    if (clustered && links.height > 0) {
        static const char *const header[9] = {"link", "via (sender)", "sent", "received", "round/s", "place/s", "load/s", "ctl/s", "RTT"};
        mt_rows rows = {0};
        for (uint32_t k = 0; k < n; k++)
            for (uint32_t j = 0; j < n; j++) {
                if (j == k) continue;
                dir_rates d = dir(by_sys, k, j);
                const mt_machine *sender = by_sys[k];
                const mt_link_track *t = sender ? mt_net_link(&sender->net, j) : NULL;
                const mt_snapshot *st = sender && sender->has_snap ? &sender->snap : NULL;
                const char *via = "-", *rtt = "-";
                if (st) {
                    mt_link_stat l = mt_snapshot_link(st, j);
                    via = MT_FMT(f, "%s %s", l.iface, addr_of(f, l.local));
                    float r = mt_snapshot_member(st, j).rtt_us;
                    if (r > 0.0f) rtt = mt_fmt_us(f, r);
                }
#define FR(i) (t ? sh(f, mt_meter_per_s(&t->tx_frames[i])) : "?")
                tui_table_cell c[9] = {
                    mt_cell_line(f, MT_LINE(f, mt_fgb(MT_FMT(f, "%u", k), mt_sys_color(k)), mt_label(" → "), mt_fgb(MT_FMT(f, "%u", j), mt_sys_color(j)))),
                    mt_cell_span(f, mt_fgs(via, MT_DIM)),
                    mt_cell_span(f, mt_fgb(d.has_tx ? human(f, d.tx) : "?", MT_FG)),
                    mt_cell_span(f, mt_fgs(d.has_rx ? human(f, d.rx) : "?", MT_DIM)),
                    mt_cell_span(f, mt_fgs(FR(MESH_KIND_ROUND), MT_WARN)),
                    mt_cell_str(f, FR(MESH_KIND_PLACE)),
                    mt_cell_str(f, FR(MESH_KIND_LOAD)),
                    mt_cell_str(f, FR(MESH_KIND_CTL)),
                    mt_cell_span(f, mt_fgs(rtt, MT_ACCENT)),
                };
#undef FR
                mt_rows_push(f, &rows, mt_row(f, c, 9));
            }
        static const tui_constraint w[9] = {
            {TUI_C_LENGTH, 7, 0}, {TUI_C_LENGTH, 32, 0}, {TUI_C_LENGTH, 11, 0}, {TUI_C_LENGTH, 11, 0}, {TUI_C_LENGTH, 8, 0},
            {TUI_C_LENGTH, 8, 0}, {TUI_C_LENGTH, 7, 0},  {TUI_C_LENGTH, 6, 0},  {TUI_C_FILL, 1, 0},
        };
        tui_table_row hdr = mt_header(f, header, 9);
        table_in(f, &rows, &hdr, w, 9, MT_LINE(f, mt_fgb(" links ", MT_FG), mt_label("frames/s as sent ")), links);
    }
    if (nics.height == 0) return;
    static const char *const header[10] = {"machine", "interface", "to sys", "rx", "tx", "pkt/s in/out", "err/s", "drop/s", "mesh", "rx+tx, 1 s/col"};
    mt_rows rows = {0};
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_machine *m = &app->machines[i];
        tui_span name = mt_fgb(mt_name(f, m), MT_MACHINE[i % 8]);
        if (!m->has_snap) {
            tui_table_cell c[2] = {mt_cell_span(f, name), mt_cell_span(f, mt_fgs(m->status, MT_BAD))};
            mt_rows_push(f, &rows, mt_row(f, c, 2));
            continue;
        }
        bool first = true;
        mt_nic xs[MESH_MAX_NICS];
        size_t nx = mt_snapshot_nics(&m->snap, xs);
        for (size_t q = 0; q <= nx; q++) {
            const char *iface, *lk;
            const mt_nic_track *t;
            if (q < nx) {
                iface = tui_arena_strdup(f->a, xs[q].name);
                lk = mt_mask_list(f, xs[q].links);
                t = xs[q].count > 0 ? mt_net_nic(&m->net, xs[q].name) : NULL;
            } else {
                iface = MT_FMT(f, "total (%u)", mt_snapshot_net_total(&m->snap).count);
                lk = "";
                t = &m->net.total;
            }
            tui_table_row row = tui_table_row_new();
            tui_table_row_push(f->a, &row, mt_cell_span(f, first ? name : tui_span_raw("")));
            first = false;
            tui_table_row_push(f->a, &row, mt_cell_str(f, iface));
            tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgs(lk, MT_DIM)));
            if (t) {
                double pr, pt;
                mt_nic_packets(t, &pr, &pt);
                double e = mt_nic_errors(t), d = mt_nic_drops(t);
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgb(human(f, mt_nic_rx(t)), MT_FG)));
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgb(human(f, mt_nic_tx(t)), MT_FG)));
                tui_table_row_push(f->a, &row, mt_cell_str(f, MT_FMT(f, "%s/%s", sh(f, pr), sh(f, pt))));
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_styled(sh(f, e), bad_style(e))));
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_styled(sh(f, d), bad_style(d))));
            } else {
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgs("gone", MT_BAD)));
            }
            if (q == nx) {
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgs(share_text(f, &m->net), MT_ACCENT)));
                tui_table_row_push(f->a, &row, mt_cell_span(f, mt_fgs(spark(f, &m->net, 30), MT_ACCENT)));
            }
            mt_rows_push(f, &rows, row);
        }
        const mt_meter *c = m->net.ctl, *h = m->net.http;
        tui_table_cell cc[6] = {
            mt_cell_str(f, ""),
            mt_cell_span(f, mt_label("ctl / http")),
            mt_cell_str(f, ""),
            mt_cell_span(f, mt_fgs(MT_FMT(f, "%s / %s", sh(f, mt_meter_per_s(&c[0])), sh(f, mt_meter_per_s(&h[0]))), MT_DIM)),
            mt_cell_span(f, mt_fgs(MT_FMT(f, "%s / %s", sh(f, mt_meter_per_s(&c[1])), sh(f, mt_meter_per_s(&h[1]))), MT_DIM)),
            mt_cell_span(f, mt_fgs(MT_FMT(f, "%s req, %s conn/s", sh(f, mt_meter_per_s(&c[2])), sh(f, mt_meter_per_s(&h[2]))), MT_DIM)),
        };
        mt_rows_push(f, &rows, mt_row(f, cc, 6));
    }
    static const tui_constraint w[10] = {
        {TUI_C_LENGTH, 16, 0}, {TUI_C_LENGTH, 16, 0}, {TUI_C_LENGTH, 6, 0}, {TUI_C_LENGTH, 11, 0}, {TUI_C_LENGTH, 11, 0},
        {TUI_C_LENGTH, 19, 0}, {TUI_C_LENGTH, 6, 0},  {TUI_C_LENGTH, 6, 0}, {TUI_C_LENGTH, 5, 0},  {TUI_C_FILL, 1, 0},
    };
    tui_table_row hdr = mt_header(f, header, 10);
    tui_line title = MT_LINE(f, mt_fgb(" interfaces ", MT_FG),
                             mt_label("the ones carrying peer links, and each machine's total (no loopback, container, VM or tunnel interfaces) "));
    table_in(f, &rows, &hdr, w, 10, title, nics);
}
