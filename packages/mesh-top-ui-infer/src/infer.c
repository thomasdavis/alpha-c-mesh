/* ui.rs's "5 infer" section, ported. */
#include "mesh/top_ui_infer.h"

#include "tui/chart.h"

#include <math.h>

typedef struct {
    const char *name;
    tui_color color;
    tui_point *pts;
    size_t n;
} curve;

/* The finite points of an f32 (x, y) series at two view offsets. */
static size_t finite_pts(mt_frame *f, const mt_section *p, uint32_t xo, uint32_t yo, size_t count, tui_point **out) {
    tui_point *pts = tui_arena_alloc(f->a, count * sizeof *pts);
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        float x = mt_section_view_f32(p, xo + 4 * i), y = mt_section_view_f32(p, yo + 4 * i);
        if (isfinite(x) && isfinite(y)) pts[n++] = (tui_point){(double)x, (double)y};
    }
    *out = pts;
    return n;
}

void mt_draw_infer(mt_frame *f, tui_rect area, const mt_app *app) {
    const mt_machine *m = mt_app_focused(app);
    tui_color color;
    tui_line title = mt_machine_title(f, m, true, "infer: model vs sin(pi x)", &color);
    tui_block block = mt_panel(f, title, color);
    tui_rect inner = tui_block_inner(&block, area);
    mt_block(f, &block, area);
    const mt_section *inf = mt_bound(f, inner, m, "infer");
    if (!inf) return;
    tui_rect hs[2];
    tui_hsplit(inner, 2, (tui_constraint[]){TUI_FILL(1), TUI_LENGTH(30)}, hs);
    /* The target samples live in train's view (infer reads train's params the same way). */
    const mt_section *train = mt_machine_program(m, "train");
    tui_point *target = NULL;
    size_t ntarget = train ? finite_pts(f, train, MESH_TRAIN_T_X, MESH_TRAIN_T_Y, 256, &target) : 0;
    curve *curves = tui_arena_alloc(f->a, (app->nmachines + 1) * sizeof *curves);
    size_t nc = 0;
    curves[nc].name = mt_name(f, m);
    curves[nc].color = MT_MACHINE[app->focus % 8];
    curves[nc].n = finite_pts(f, inf, MESH_INFER_I_X, MESH_INFER_I_Y, MESH_INFER_POINTS, &curves[nc].pts);
    nc++;
    for (size_t i = 0; i < app->nmachines; i++) {
        const mt_section *op;
        if (i == app->focus || !(op = mt_machine_program(&app->machines[i], "infer"))) continue;
        curves[nc].name = mt_name(f, &app->machines[i]);
        curves[nc].color = MT_MACHINE[i % 8];
        curves[nc].n = finite_pts(f, op, MESH_INFER_I_X, MESH_INFER_I_Y, MESH_INFER_POINTS, &curves[nc].pts);
        nc++;
    }
    double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
    for (size_t i = 0; i < ntarget; i++) {
        x0 = fmin(x0, target[i].x);
        x1 = fmax(x1, target[i].x);
        y0 = fmin(y0, target[i].y);
        y1 = fmax(y1, target[i].y);
    }
    for (size_t c = 0; c < nc; c++)
        for (size_t i = 0; i < curves[c].n; i++) {
            x0 = fmin(x0, curves[c].pts[i].x);
            x1 = fmax(x1, curves[c].pts[i].x);
            y0 = fmin(y0, curves[c].pts[i].y);
            y1 = fmax(y1, curves[c].pts[i].y);
        }
    if (!isfinite(x0) || x1 <= x0) {
        x0 = -1.0;
        x1 = 1.0;
    }
    if (!isfinite(y0) || y1 <= y0) {
        y0 = -1.0;
        y1 = 1.0;
    }
    double pad = (y1 - y0) * 0.08;
    y0 -= pad;
    y1 += pad;
    tui_dataset *sets = tui_arena_alloc(f->a, (nc + 1) * sizeof *sets);
    sets[0] = tui_dataset_new();
    sets[0].has_name = true;
    sets[0].name = tui_line_from_str(f->a, "target sin(pi x)");
    sets[0].marker = TUI_MARKER_DOT;
    sets[0].graph_type = TUI_GRAPH_SCATTER;
    sets[0].style = tui_fg(MT_DIM);
    sets[0].data = target;
    sets[0].n = ntarget;
    /* Draw the other machines first so the focused curve stays on top. */
    for (size_t c = 0; c < nc; c++) {
        const curve *cv = &curves[nc - 1 - c];
        tui_dataset *d = &sets[1 + c];
        *d = tui_dataset_new();
        d->has_name = true;
        d->name = tui_line_from_str(f->a, cv->name);
        d->marker = TUI_MARKER_BRAILLE;
        d->graph_type = TUI_GRAPH_LINE;
        d->style = tui_fg(cv->color);
        d->data = cv->pts;
        d->n = cv->n;
    }
    tui_line *xl = tui_arena_alloc(f->a, 2 * sizeof *xl), *yl = tui_arena_alloc(f->a, 2 * sizeof *yl);
    xl[0] = tui_line_from_str(f->a, mt_ff(f, x0, 2));
    xl[1] = tui_line_from_str(f->a, MT_FMT(f, "x %s", mt_ff(f, x1, 2)));
    yl[0] = tui_line_from_str(f->a, mt_ff(f, y0, 2));
    yl[1] = tui_line_from_str(f->a, mt_ff(f, y1, 2));
    tui_chart chart = tui_chart_new(sets, nc + 1);
    chart.x_axis = tui_axis_new();
    chart.x_axis.style = tui_fg(MT_DIM);
    chart.x_axis.bounds[0] = x0;
    chart.x_axis.bounds[1] = x1;
    chart.x_axis.labels = xl;
    chart.x_axis.nlabels = 2;
    chart.y_axis = tui_axis_new();
    chart.y_axis.style = tui_fg(MT_DIM);
    chart.y_axis.bounds[0] = y0;
    chart.y_axis.bounds[1] = y1;
    chart.y_axis.labels = yl;
    chart.y_axis.nlabels = 2;
    chart.has_legend = true;
    chart.legend_position = TUI_LEGEND_BOTTOM_RIGHT;
    chart.style = tui_style_bg(tui_style_new(), MT_BG);
    tui_chart_render(f->a, &chart, hs[0], f->buf);

    mt_lines lines = {0};
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("infer "), mt_styled(mt_section_state_name(inf), mt_state_style(inf))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("points "), mt_val(MT_FMT(f, "%u", MESH_INFER_POINTS))));
    mt_lines_push(f, &lines, tui_line_span(f->a, mt_label("y = a + bx + cx² + dx³")));
    /* The params infer's last run read from train's view, and train's step then. */
    static const char *const pn[4] = {"a", "b", "c", "d"};
    for (size_t k = 0; k < 4; k++)
        mt_lines_push(f, &lines,
                      MT_LINE(f, mt_label(MT_FMT(f, "%s  ", pn[k])), mt_val(mt_ffp(f, mt_section_view_f32(inf, MESH_INFER_I_PARAMS + 4 * k), 6))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("runs "), mt_val(MT_FMT(f, "%u", mt_section_view_u32(inf, MESH_INFER_I_RUNS)))));
    mt_lines_push(f, &lines, MT_LINE(f, mt_label("train step "), mt_val(MT_FMT(f, "%u", mt_section_view_u32(inf, MESH_INFER_I_TRAIN_STEP)))));
    if (!train) mt_lines_push(f, &lines, tui_line_span(f->a, mt_fgs("train is not loaded", MT_WARN)));
    tui_block b = tui_block_new();
    b.borders = TUI_BORDER_LEFT;
    b.border_style = tui_fg(MT_FAINT);
    tui_paragraph p = tui_paragraph_new(mt_text(f, &lines));
    p.block = &b;
    tui_paragraph_render(f->a, &p, hs[1], f->buf);
}
