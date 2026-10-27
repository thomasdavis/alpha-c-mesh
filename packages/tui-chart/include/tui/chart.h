/* @ajax/tui-chart: ratatui's Chart, Axis and Dataset: datasets drawn on a canvas (scatter points,
 * lines, bars or filled areas), x and y axes with labels and titles, and a legend placed where it
 * fits. */
#ifndef TUI_CHART_H
#define TUI_CHART_H

#include "tui/canvas.h"
#include "tui/layout.h"

typedef enum { TUI_GRAPH_SCATTER, TUI_GRAPH_LINE, TUI_GRAPH_BAR, TUI_GRAPH_AREA } tui_graph_type;

typedef enum {
    TUI_LEGEND_TOP,
    TUI_LEGEND_TOP_RIGHT, /* the default */
    TUI_LEGEND_TOP_LEFT,
    TUI_LEGEND_LEFT,
    TUI_LEGEND_RIGHT,
    TUI_LEGEND_BOTTOM,
    TUI_LEGEND_BOTTOM_RIGHT,
    TUI_LEGEND_BOTTOM_LEFT,
} tui_legend_position;

typedef struct {
    bool has_title;
    tui_line title;
    double bounds[2];
    const tui_line *labels;
    size_t nlabels;
    tui_style style;
    int8_t labels_alignment; /* TUI_ALIGN_LEFT */
} tui_axis;

typedef struct {
    bool has_name;
    tui_line name;
    const tui_point *data;
    size_t n;
    tui_marker marker; /* TUI_MARKER_DOT by default */
    tui_graph_type graph_type;
    tui_style style;
    double fill_to_y;
} tui_dataset;

typedef struct {
    const tui_block *block;
    tui_axis x_axis, y_axis;
    const tui_dataset *datasets;
    size_t ndatasets;
    tui_style style;
    tui_constraint hidden_legend[2]; /* Ratio(1, 4) each */
    bool has_legend;                 /* true */
    tui_legend_position legend_position;
} tui_chart;

tui_chart tui_chart_new(const tui_dataset *datasets, size_t n);
static inline tui_axis tui_axis_new(void) {
    tui_axis x = {0};
    x.labels_alignment = TUI_ALIGN_LEFT;
    return x;
}
static inline tui_dataset tui_dataset_new(void) {
    tui_dataset d = {0};
    d.marker = TUI_MARKER_DOT;
    return d;
}

void tui_chart_render(tui_arena *a, const tui_chart *c, tui_rect area, tui_buffer *buf);

#endif
