/* Braille dots at the corners, a clipped diagonal, half blocks, layers. */
#include "mesh/check.h"
#include "tui/canvas.h"

#include <string.h>

int main(void) {
    tui_arena a = {0};
    tui_buffer b;
    tui_buffer_init(&b, (tui_rect){0, 0, 2, 2});
    double xb[2] = {0, 1}, yb[2] = {0, 1};
    tui_canvas *c = tui_canvas_new(&a, 2, 2, xb, yb, TUI_MARKER_BRAILLE);
    tui_point pts[2] = {{0, 1}, {1, 0}};
    tui_canvas_points(c, pts, 2, TUI_RED);
    tui_canvas_render(&a, c, NULL, TUI_RESET, b.area, &b);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 0)), "⠁"); /* top left dot */
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 1)), "⢀"); /* bottom right dot */
    CHECK(tui_color_eq(tui_buffer_cell(&b, 1, 1)->fg, TUI_RED));
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 0)), " ");
    /* A diagonal from far outside is clipped to the window. */
    tui_buffer_clear(&b, b.area);
    c = tui_canvas_new(&a, 2, 2, xb, yb, TUI_MARKER_DOT);
    tui_canvas_line(c, -1, -1, 2, 2, TUI_BLUE);
    tui_canvas_render(&a, c, NULL, TUI_RESET, b.area, &b);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 1)), "•");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 0)), "•");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 0)), " ");
    /* Half blocks: two pixels a cell. */
    tui_buffer_clear(&b, b.area);
    c = tui_canvas_new(&a, 2, 2, xb, yb, TUI_MARKER_HALF_BLOCK);
    tui_point top[1] = {{0, 1}};
    tui_canvas_points(c, top, 1, TUI_GREEN);
    tui_canvas_marker(c, TUI_MARKER_HALF_BLOCK);
    tui_point bottom[1] = {{1, 0}};
    tui_canvas_points(c, bottom, 1, TUI_RED);
    tui_canvas_render(&a, c, NULL, TUI_RESET, b.area, &b);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 0)), "▀");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 1)), "▄");
    tui_buffer_free(&b);
    tui_arena_free(&a);
    return mesh_check_done();
}
