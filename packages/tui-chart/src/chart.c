/* ratatui-widgets 0.3.2's chart.rs, ported: layout (labels, axes, titles, legend), then the x and
 * y labels, the axes, the canvas, the titles and the legend. */
#include "tui/chart.h"

#include <string.h>

tui_chart tui_chart_new(const tui_dataset *datasets, size_t n) {
    tui_chart c = {0};
    c.x_axis = tui_axis_new();
    c.y_axis = tui_axis_new();
    c.datasets = datasets;
    c.ndatasets = n;
    c.hidden_legend[0] = TUI_RATIO(1, 4);
    c.hidden_legend[1] = TUI_RATIO(1, 4);
    c.has_legend = true;
    c.legend_position = TUI_LEGEND_TOP_RIGHT;
    return c;
}

static uint16_t lw(const tui_line *l) {
    size_t w = tui_line_width(l);
    return (uint16_t)(w > 0xFFFF ? 0xFFFF : w);
}

typedef struct {
    bool has_title_x, has_title_y, has_label_x, has_label_y, has_axis_x, has_axis_y, has_legend;
    uint16_t title_x_x, title_x_y, title_y_x, title_y_y, label_x, label_y, axis_x, axis_y;
    tui_rect legend, graph;
} chart_layout;

static bool legend_layout(tui_legend_position p, tui_rect area, uint16_t lw_, uint16_t lh, uint16_t xtw, uint16_t ytw,
                          tui_rect *out) {
    int32_t margin = (int32_t)(uint16_t)(area.height - lh); /* u16 subtraction, as ratatui's i32::from(a - b) */
    if (xtw) margin--;
    if (ytw) margin--;
    if (margin < 0) return false;
    uint16_t x = 0, y = 0;
    switch (p) {
    case TUI_LEGEND_TOP_RIGHT:
        x = (uint16_t)(tui_rect_right(area) - lw_);
        y = lw_ + ytw > area.width ? (uint16_t)(area.y + 1) : area.y;
        break;
    case TUI_LEGEND_TOP_LEFT:
        x = area.x;
        y = ytw ? (uint16_t)(area.y + 1) : area.y;
        break;
    case TUI_LEGEND_TOP: {
        uint16_t off = (uint16_t)((area.width - lw_) / 2);
        x = (uint16_t)(area.x + off);
        y = area.x + ytw > off ? (uint16_t)(area.y + 1) : area.y;
        break;
    }
    case TUI_LEGEND_LEFT:
    case TUI_LEGEND_RIGHT: {
        uint16_t yy = (uint16_t)((area.height - lh) / 2);
        if (ytw) yy++;
        if (xtw) yy = tui_sat_sub(yy, 1);
        x = p == TUI_LEGEND_LEFT ? area.x : (uint16_t)(tui_rect_right(area) - lw_);
        y = (uint16_t)(area.y + yy);
        break;
    }
    case TUI_LEGEND_BOTTOM_LEFT:
        x = area.x;
        y = xtw + lw_ > area.width ? (uint16_t)(tui_rect_bottom(area) - lh - 1) : (uint16_t)(tui_rect_bottom(area) - lh);
        break;
    case TUI_LEGEND_BOTTOM_RIGHT:
        x = (uint16_t)(tui_rect_right(area) - lw_);
        y = xtw ? (uint16_t)(tui_rect_bottom(area) - lh - 1) : (uint16_t)(tui_rect_bottom(area) - lh);
        break;
    case TUI_LEGEND_BOTTOM: {
        x = (uint16_t)(area.x + (area.width - lw_) / 2);
        y = x + lw_ > tui_rect_right(area) - xtw ? (uint16_t)(tui_rect_bottom(area) - lh - 1)
                                                 : (uint16_t)(tui_rect_bottom(area) - lh);
        break;
    }
    }
    *out = tui_rect_new(x, y, lw_, lh);
    return true;
}

static uint16_t max_label_width_left(const tui_chart *c, tui_rect area, bool has_y_axis) {
    uint16_t m = 0;
    for (size_t i = 0; i < c->y_axis.nlabels; i++) m = tui_max16(m, lw(&c->y_axis.labels[i]));
    if (c->x_axis.nlabels) {
        uint16_t first = lw(&c->x_axis.labels[0]), left;
        switch (c->x_axis.labels_alignment) {
        case TUI_ALIGN_CENTER: left = (uint16_t)(first / 2); break;
        case TUI_ALIGN_RIGHT: left = 0; break;
        default: left = tui_sat_sub(first, has_y_axis ? 1 : 0); break;
        }
        m = tui_max16(m, left);
    }
    return tui_min16(m, (uint16_t)(area.width / 3));
}

static bool layout(const tui_chart *c, tui_rect area, chart_layout *L) {
    if (area.height == 0 || area.width == 0) return false;
    memset(L, 0, sizeof *L);
    uint16_t x = area.x, y = (uint16_t)(tui_rect_bottom(area) - 1);
    if (c->x_axis.nlabels && y > area.y) {
        L->has_label_x = true;
        L->label_x = y;
        y--;
    }
    if (c->y_axis.nlabels) {
        L->has_label_y = true;
        L->label_y = x;
    }
    x = (uint16_t)(x + max_label_width_left(c, area, c->y_axis.nlabels > 0));
    if (c->x_axis.nlabels && y > area.y) {
        L->has_axis_x = true;
        L->axis_x = y;
        y--;
    }
    if (c->y_axis.nlabels && x + 1 < tui_rect_right(area)) {
        L->has_axis_y = true;
        L->axis_y = x;
        x++;
    }
    uint16_t gw = tui_sat_sub(tui_rect_right(area), x);
    uint16_t gh = tui_sat_add(tui_sat_sub(y, area.y), 1);
    L->graph = tui_rect_new(x, area.y, gw, gh);
    if (c->x_axis.has_title) {
        uint16_t w = lw(&c->x_axis.title);
        if (w < L->graph.width && L->graph.height > 2) {
            L->has_title_x = true;
            L->title_x_x = (uint16_t)(x + L->graph.width - w);
            L->title_x_y = y;
        }
    }
    if (c->y_axis.has_title) {
        uint16_t w = lw(&c->y_axis.title);
        if (w + 1 < L->graph.width && L->graph.height > 2) {
            L->has_title_y = true;
            L->title_y_x = x;
            L->title_y_y = area.y;
        }
    }
    if (c->has_legend) {
        uint16_t inner = 0;
        size_t count = 0;
        for (size_t i = 0; i < c->ndatasets; i++)
            if (c->datasets[i].has_name) {
                inner = count ? tui_max16(inner, lw(&c->datasets[i].name)) : lw(&c->datasets[i].name);
                count++;
            }
        if (count) {
            uint16_t legend_w = (uint16_t)(inner + 2), legend_h = (uint16_t)(count + 2);
            tui_rect mw, mh;
            tui_layout hl = {.vertical = false, .constraints = &c->hidden_legend[0], .n = 1, .flex = TUI_FLEX_START};
            tui_layout vl = {.vertical = true, .constraints = &c->hidden_legend[1], .n = 1, .flex = TUI_FLEX_START};
            tui_layout_split(&hl, L->graph, &mw, NULL);
            tui_layout_split(&vl, L->graph, &mh, NULL);
            if (inner > 0 && legend_w <= mw.width && legend_h <= mh.height) {
                uint16_t xtw = L->has_title_x ? lw(&c->x_axis.title) : 0;
                uint16_t ytw = L->has_title_y ? lw(&c->y_axis.title) : 0;
                L->has_legend = legend_layout(c->legend_position, L->graph, legend_w, legend_h, xtw, ytw, &L->legend);
            }
        }
    }
    return true;
}

static void render_label(tui_arena *a, tui_buffer *buf, const tui_line *label, tui_rect area, int8_t align) {
    tui_line l = *label;
    l.align = align;
    tui_line_render(a, &l, area, buf, TUI_ALIGN_NONE);
}

static void x_labels(tui_arena *a, const tui_chart *c, tui_buffer *buf, const chart_layout *L, tui_rect chart_area) {
    if (!L->has_label_x) return;
    uint16_t y = L->label_x;
    size_t n = c->x_axis.nlabels;
    if (n < 2) return;
    tui_rect g = L->graph;
    uint16_t between = (uint16_t)(g.width / n);
    uint16_t first_w = lw(&c->x_axis.labels[0]);
    uint16_t min_x, max_x;
    switch (c->x_axis.labels_alignment) {
    case TUI_ALIGN_CENTER:
        min_x = chart_area.x;
        max_x = (uint16_t)(g.x + tui_min16(between, first_w));
        break;
    case TUI_ALIGN_RIGHT:
        min_x = tui_sat_sub(g.x, 1);
        max_x = (uint16_t)(g.x + between);
        break;
    default:
        min_x = chart_area.x;
        max_x = g.x;
        break;
    }
    int8_t first_align = c->x_axis.labels_alignment == TUI_ALIGN_CENTER ? TUI_ALIGN_CENTER
                         : c->x_axis.labels_alignment == TUI_ALIGN_RIGHT ? TUI_ALIGN_LEFT
                                                                         : TUI_ALIGN_RIGHT;
    render_label(a, buf, &c->x_axis.labels[0], tui_rect_new(min_x, y, (uint16_t)(max_x - min_x), 1), first_align);
    for (size_t i = 1; i + 1 < n; i++) {
        uint16_t x = (uint16_t)(g.x + i * between + 1);
        render_label(a, buf, &c->x_axis.labels[i], tui_rect_new(x, y, tui_sat_sub(between, 1), 1), TUI_ALIGN_CENTER);
    }
    uint16_t x = (uint16_t)(tui_rect_right(g) - between);
    render_label(a, buf, &c->x_axis.labels[n - 1], tui_rect_new(x, y, between, 1), TUI_ALIGN_RIGHT);
}

static void y_labels(tui_arena *a, const tui_chart *c, tui_buffer *buf, const chart_layout *L, tui_rect chart_area) {
    if (!L->has_label_y) return;
    size_t n = c->y_axis.nlabels;
    if (n < 2) return;
    tui_rect g = L->graph;
    for (size_t i = 0; i < n; i++) {
        uint16_t dy = (uint16_t)(i * (uint16_t)(g.height - 1) / (n - 1));
        if (dy < tui_rect_bottom(g)) {
            tui_rect r = tui_rect_new(L->label_y, (uint16_t)(tui_sat_sub(tui_rect_bottom(g), 1) - dy),
                                      tui_sat_sub((uint16_t)(g.x - chart_area.x), 1), 1);
            render_label(a, buf, &c->y_axis.labels[i], r, c->y_axis.labels_alignment);
        }
    }
}

void tui_chart_render(tui_arena *a, const tui_chart *c, tui_rect area, tui_buffer *buf) {
    tui_buffer_set_style(buf, area, c->style);
    if (c->block) tui_block_render(a, c->block, area, buf);
    tui_rect chart_area = tui_block_inner_opt(c->block, area);
    chart_layout L;
    if (!layout(c, chart_area, &L)) return;
    tui_rect g = L.graph;
    const tui_cell *corner = tui_buffer_cell_c(buf, area.x, area.y);
    tui_style original = corner ? tui_cell_style(corner) : tui_style_new();
    x_labels(a, c, buf, &L, chart_area);
    y_labels(a, c, buf, &L, chart_area);
    if (L.has_axis_x)
        for (uint16_t x = g.x; x < tui_rect_right(g); x++) {
            tui_cell *cl = tui_buffer_cell(buf, x, L.axis_x);
            tui_cell_set_str(cl, "─");
            tui_cell_set_style(cl, c->x_axis.style);
        }
    if (L.has_axis_y)
        for (uint16_t y = g.y; y < tui_rect_bottom(g); y++) {
            tui_cell *cl = tui_buffer_cell(buf, L.axis_y, y);
            tui_cell_set_str(cl, "│");
            tui_cell_set_style(cl, c->y_axis.style);
        }
    if (L.has_axis_x && L.has_axis_y) {
        tui_cell *cl = tui_buffer_cell(buf, L.axis_y, L.axis_x);
        tui_cell_set_str(cl, "└");
        tui_cell_set_style(cl, c->x_axis.style);
    }
    /* The canvas: one marker change per dataset, points, then its graph. */
    tui_canvas *cv = tui_canvas_new(a, g.width, g.height, c->x_axis.bounds, c->y_axis.bounds, TUI_MARKER_BRAILLE);
    for (size_t i = 0; i < c->ndatasets; i++) {
        const tui_dataset *d = &c->datasets[i];
        tui_canvas_marker(cv, d->marker);
        tui_color color = d->style.has_fg ? d->style.fg : TUI_RESET;
        tui_canvas_points(cv, d->data, d->n, color);
        switch (d->graph_type) {
        case TUI_GRAPH_LINE:
            for (size_t k = 0; k + 1 < d->n; k++)
                tui_canvas_line(cv, d->data[k].x, d->data[k].y, d->data[k + 1].x, d->data[k + 1].y, color);
            break;
        case TUI_GRAPH_BAR:
            for (size_t k = 0; k < d->n; k++) tui_canvas_line(cv, d->data[k].x, 0.0, d->data[k].x, d->data[k].y, color);
            break;
        case TUI_GRAPH_AREA:
            for (size_t k = 0; k + 1 < d->n; k++)
                tui_canvas_filled_line(cv, d->data[k].x, d->data[k].y, d->data[k + 1].x, d->data[k + 1].y, d->fill_to_y, color);
            break;
        case TUI_GRAPH_SCATTER:
            break;
        }
    }
    tui_canvas_render(a, cv, NULL, c->style.has_bg ? c->style.bg : TUI_RESET, g, buf);
    if (L.has_title_x) {
        uint16_t w = tui_min16(tui_sat_sub(tui_rect_right(g), L.title_x_x), lw(&c->x_axis.title));
        tui_buffer_set_style(buf, (tui_rect){L.title_x_x, L.title_x_y, w, 1}, original);
        tui_buffer_set_line(buf, L.title_x_x, L.title_x_y, &c->x_axis.title, w);
    }
    if (L.has_title_y) {
        uint16_t w = tui_min16(tui_sat_sub(tui_rect_right(g), L.title_y_x), lw(&c->y_axis.title));
        tui_buffer_set_style(buf, (tui_rect){L.title_y_x, L.title_y_y, w, 1}, original);
        tui_buffer_set_line(buf, L.title_y_x, L.title_y_y, &c->y_axis.title, w);
    }
    if (L.has_legend) {
        tui_buffer_set_style(buf, L.legend, original);
        tui_block lb = tui_block_bordered();
        tui_block_render(a, &lb, L.legend, buf);
        size_t i = 0;
        for (size_t k = 0; k < c->ndatasets; k++) {
            const tui_dataset *d = &c->datasets[k];
            if (!d->has_name) continue;
            tui_line name = d->name;
            name.style = tui_style_patch(name.style, d->style);
            tui_line_render(a, &name, (tui_rect){(uint16_t)(L.legend.x + 1), (uint16_t)(L.legend.y + 1 + i), (uint16_t)(L.legend.width - 2), 1},
                            buf, TUI_ALIGN_NONE);
            i++;
        }
    }
}
