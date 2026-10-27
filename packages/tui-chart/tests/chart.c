/* A line chart with both axes' labels and a legend: the layout ratatui gives it. */
#include "mesh/check.h"
#include "tui/chart.h"

#include <string.h>

static const char *row(const tui_buffer *b, uint16_t y) {
    static char out[1024];
    size_t n = 0;
    for (uint16_t x = 0; x < b->area.width; x++) {
        const char *s = tui_cell_symbol(tui_buffer_cell_c(b, x, y));
        size_t k = strlen(s);
        memcpy(out + n, s, k);
        n += k;
    }
    out[n] = 0;
    return out;
}

int main(void) {
    tui_arena a = {0};
    tui_buffer b;
    tui_buffer_init(&b, (tui_rect){0, 0, 20, 8});
    tui_point pts[3] = {{0, 0}, {5, 5}, {10, 10}};
    tui_dataset d = tui_dataset_new();
    d.has_name = true;
    d.name = tui_line_from_str(&a, "data");
    d.data = pts;
    d.n = 3;
    d.marker = TUI_MARKER_DOT;
    d.graph_type = TUI_GRAPH_LINE;
    tui_chart c = tui_chart_new(&d, 1);
    tui_line xl[2] = {tui_line_from_str(&a, "0"), tui_line_from_str(&a, "10")};
    tui_line yl[2] = {tui_line_from_str(&a, "0"), tui_line_from_str(&a, "10")};
    c.x_axis.bounds[0] = 0;
    c.x_axis.bounds[1] = 10;
    c.x_axis.labels = xl;
    c.x_axis.nlabels = 2;
    c.y_axis.bounds[0] = 0;
    c.y_axis.bounds[1] = 10;
    c.y_axis.labels = yl;
    c.y_axis.nlabels = 2;
    tui_chart_render(&a, &c, b.area, &b);
    CHECK_S(row(&b, 7), "  0               10");
    CHECK_S(row(&b, 6), "  └─────────────────");
    CHECK(strstr(row(&b, 0), "10│") == row(&b, 0));
    tui_buffer_free(&b);
    tui_arena_free(&a);
    tui_layout_cache_clear();
    return mesh_check_done();
}
