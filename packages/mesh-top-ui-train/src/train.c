/* ui.rs's "4 train" section, ported. */
#include "mesh/top_ui_train.h"

#include "tui/chart.h"

#include <math.h>
#include <string.h>

tui_line mt_identity_line(mt_frame *f, const mt_app *app) {
    const char *lock = "lockstep";
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_machine *m = &app->machines[i];
        if (!m->has_snap) continue;
        char name[3 * MESH_NAME_BYTES + 1];
        uint64_t step;
        uint8_t view[32];
        mt_snapshot_lock_pair(&m->snap, name, &step, view);
        if (*name) {
            lock = tui_arena_strdup(f->a, name);
            break;
        }
    }
    mt_identity id;
    if (!mt_params_identity(app, &id))
        return MT_LINE(f, mt_label(MT_FMT(f, " %s params bitwise identical: ", lock)), mt_fgs("waiting for a step every machine reports", MT_WARN));
    tui_line v = MT_LINE(f, mt_label(MT_FMT(f, " %s params bitwise identical across %zu: ", lock, id.n)),
                         id.identical ? mt_styled("YES", mt_fg_bg_bold(MT_BG, MT_GOOD)) : mt_styled("NO", mt_fg_bg_bold(TUI_WHITE, MT_BAD)),
                         mt_label(MT_FMT(f, " (at step %llu)", (unsigned long long)id.step)));
    if (!id.identical) {
        const char *j = "";
        for (size_t i = 0; i < id.ndiffer; i++) j = MT_FMT(f, "%s%s%s", j, i ? ", " : "", id.differ[i]);
        tui_line_push(f->a, &v, mt_fgb(MT_FMT(f, "  differ: %s", j), MT_BAD));
    }
    return v;
}

static void draw_train_machine(mt_frame *f, tui_rect area, const mt_machine *m, size_t i, bool focused) {
    tui_color color;
    tui_line title = mt_machine_title(f, m, focused, "train", &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    const mt_section *t = mt_bound(f, inner, m, "train");
    if (!t) return;
    const mt_snapshot *s = &m->snap;
    tui_rect hs[2];
    tui_hsplit(inner, 2, (tui_constraint[]){TUI_FILL(1), TUI_LENGTH(30)}, hs);

    tui_point *pts = tui_arena_alloc(f->a, (m->nlosses ? m->nlosses : 1) * sizeof *pts);
    size_t np = 0;
    for (size_t k = 0; k < m->nlosses; k++) {
        float l = m->losses[k].loss;
        if (isfinite(l) && l > 0.0f) pts[np++] = (tui_point){(double)m->losses[k].step, log10((double)l)};
    }
    double x0 = 0.0, x1 = 1.0;
    if (np) {
        x0 = pts[0].x;
        x1 = fmax(pts[np - 1].x, pts[0].x + 1.0);
    }
    double lo = INFINITY, hi = -INFINITY;
    for (size_t k = 0; k < np; k++) {
        lo = fmin(lo, pts[k].y);
        hi = fmax(hi, pts[k].y);
    }
    if (isfinite(lo)) {
        double fl = floor(lo);
        hi = fmax(ceil(hi), fl + 1.0);
        lo = fl;
    } else {
        lo = -3.0;
        hi = 0.0;
    }
    double ev[3] = {lo, (lo + hi) / 2.0, hi};
    tui_line *ylabels = tui_arena_alloc(f->a, 3 * sizeof *ylabels);
    for (int k = 0; k < 3; k++) ylabels[k] = tui_line_from_str(f->a, mt_fe(f, pow(10.0, ev[k]), 0));
    tui_line *xlabels = tui_arena_alloc(f->a, 2 * sizeof *xlabels);
    xlabels[0] = tui_line_from_str(f->a, mt_ff(f, x0, 0));
    xlabels[1] = tui_line_from_str(f->a, MT_FMT(f, "step %s", mt_ff(f, x1, 0)));
    tui_dataset ds = tui_dataset_new();
    ds.has_name = true;
    ds.name = tui_line_from_str(f->a, MT_FMT(f, "loss, log scale, %zu pts", np));
    ds.marker = TUI_MARKER_BRAILLE;
    ds.graph_type = TUI_GRAPH_LINE;
    ds.style = tui_fg(MT_MACHINE[i % 8]);
    ds.data = pts;
    ds.n = np;
    tui_chart chart = tui_chart_new(&ds, 1);
    chart.x_axis = tui_axis_new();
    chart.x_axis.style = tui_fg(MT_DIM);
    chart.x_axis.bounds[0] = x0;
    chart.x_axis.bounds[1] = x1;
    chart.x_axis.labels = xlabels;
    chart.x_axis.nlabels = 2;
    chart.y_axis = tui_axis_new();
    chart.y_axis.style = tui_fg(MT_DIM);
    chart.y_axis.bounds[0] = lo;
    chart.y_axis.bounds[1] = hi;
    chart.y_axis.labels = ylabels;
    chart.y_axis.nlabels = 3;
    chart.style = tui_style_bg(tui_style_new(), MT_BG);
    tui_chart_render(f->a, &chart, hs[0], f->buf);

    uint32_t err = mt_section_view_u32(t, MESH_TRAIN_T_ERR);
    float rtt = mt_snapshot_host_f32(s, MESH_S_PEER_RTT_US);
    mt_loss ls[256];
    size_t nl = mt_train_losses(t, ls);
    float last = nl ? ls[nl - 1].loss : NAN;
    mt_lines lines = {0};
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("train "), mt_styled(mt_section_state_name(t), mt_state_style(t))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("step    "), mt_val(MT_FMT(f, "%llu", (unsigned long long)mt_train_next_step(t)))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("steps/s "), mt_fgb(mt_ff(f, mt_rate_per_s(&m->train_steps), 0), MT_WARN)));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("loss    "), mt_val(mt_fe(f, last, 3))));
    static const char *const pn[4] = {"a", "b", "c", "d"};
    for (size_t k = 0; k < 4; k++)
        mt_lines_push(f, &lines, MT_LINE(f, mt_label(MT_FMT(f, "%s  ", pn[k])), mt_val(mt_ffp(f, mt_section_view_f32(t, MESH_TRAIN_T_PARAMS + 4 * k), 6))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("peer "), mt_peer_span(f, s)));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("RTT  "), mt_val(rtt > 0.0f ? MT_FMT(f, "%sus", mt_ff(f, rtt, 1)) : "-"),
                                     mt_label(MT_FMT(f, "  round %u", mt_section_view_u32(t, MESH_TRAIN_T_ROUND)))));
    uint32_t wk;
    float wus;
    if (mt_slowest_wait(s, &wk, &wus))
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("wait "), mt_val(mt_fmt_us(f, wus)), mt_label(" /round for "),
                                         mt_fgb(MT_FMT(f, "sys %u", wk), mt_sys_color(wk))));
    if (err != 0)
        mt_lines_push(f, &lines, tui_line_span(f->a, mt_styled(" T_ERR: peer timed out ", mt_fg_bg_bold(TUI_WHITE, MT_BAD))));
    else
        mt_lines_push(f, &lines, MT_LINE(f, mt_label("T_ERR   "), mt_fgs("0", MT_GOOD)));
    tui_block b = tui_block_new();
    b.borders = TUI_BORDER_LEFT;
    b.border_style = tui_fg(MT_FAINT);
    tui_paragraph p = tui_paragraph_new(mt_text(f, &lines));
    p.block = &b;
    tui_paragraph_render(f->a, &p, hs[1], f->buf);
}

void mt_draw_train(mt_frame *f, tui_rect area, const mt_app *app) {
    size_t n = app->nmachines;
    size_t nc = n + (n >= 2);
    tui_constraint *cons = tui_arena_alloc(f->a, nc * sizeof *cons);
    for (size_t i = 0; i < n; i++) cons[i] = TUI_FILL(1);
    if (n >= 2) cons[n] = TUI_LENGTH(1);
    tui_rect *rows = tui_arena_alloc(f->a, nc * sizeof *rows);
    tui_vsplit(area, nc, cons, rows);
    for (size_t i = 0; i < n; i++) draw_train_machine(f, rows[i], &app->machines[i], i, i == app->focus);
    if (n >= 2) mt_par_line(f, mt_identity_line(f, app), rows[n]);
}
