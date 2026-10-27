/* @ajax/tui-canvas: ratatui's Canvas Context. Shapes are drawn in world coordinates (x_bounds,
 * y_bounds) onto a grid of the marker's resolution: braille (2x4 dots a cell), half blocks (1x2),
 * or one character a cell (dot, block, bar). Changing the marker starts a new layer; layers are
 * drawn in order, each cell taking the latest layer's symbol and colours. */
#ifndef TUI_CANVAS_H
#define TUI_CANVAS_H

#include "tui/block.h"

typedef enum {
    TUI_MARKER_DOT,
    TUI_MARKER_BLOCK,
    TUI_MARKER_BAR,
    TUI_MARKER_BRAILLE,
    TUI_MARKER_HALF_BLOCK,
} tui_marker;

typedef struct tui_canvas tui_canvas;

/* A point in world coordinates (ratatui's (f64, f64)). */
typedef struct {
    double x, y;
} tui_point;

/* A context for a canvas of width x height cells (Context::new). Lives in the arena. */
tui_canvas *tui_canvas_new(tui_arena *a, uint16_t width, uint16_t height, const double x_bounds[2],
                           const double y_bounds[2], tui_marker marker);
void tui_canvas_marker(tui_canvas *c, tui_marker m); /* Context::marker */
void tui_canvas_points(tui_canvas *c, const tui_point *pts, size_t n, tui_color color);
void tui_canvas_line(tui_canvas *c, double x1, double y1, double x2, double y2, tui_color color);
void tui_canvas_filled_line(tui_canvas *c, double x1, double y1, double x2, double y2, double fill_to_y, tui_color color);
void tui_canvas_bar(tui_canvas *c, double x, double y, tui_color color); /* a line from (x, 0) to (x, y) */
void tui_canvas_print(tui_canvas *c, double x, double y, tui_line line);
/* Canvas::render with the context painted: the block, the background, every layer, the labels. */
void tui_canvas_render(tui_arena *a, tui_canvas *c, const tui_block *block, tui_color background, tui_rect area,
                       tui_buffer *buf);
/* Canvas::render when the area isn't known before painting: paint(ctx, arg) is called on a
 * context sized to the area's inside (ratatui calls the paint closure the same way). */
void tui_canvas_widget(tui_arena *a, const tui_block *block, tui_color background, const double x_bounds[2],
                       const double y_bounds[2], tui_marker marker, void (*paint)(tui_canvas *c, void *arg), void *arg,
                       tui_rect area, tui_buffer *buf);

#endif
