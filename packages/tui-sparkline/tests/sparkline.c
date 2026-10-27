/* Bars in eighths, scaled to the largest value or to max, bottom up. */
#include "mesh/check.h"
#include "tui/sparkline.h"

#include <string.h>

int main(void) {
    tui_arena a = {0};
    tui_buffer b;
    tui_buffer_init(&b, (tui_rect){0, 0, 4, 2});
    uint64_t data[3] = {0, 8, 16};
    tui_sparkline s = {.data = data, .n = 3};
    tui_sparkline_render(&a, &s, b.area, &b);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 2, 0)), "█");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 1)), "█");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 0)), " ");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 1)), " ");
    s.has_max = true;
    s.max = 64;
    tui_sparkline_render(&a, &s, b.area, &b);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 2, 1)), "▄");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 1)), "▂");
    uint64_t huge[1] = {UINT64_MAX};
    tui_sparkline h = {.data = huge, .n = 1, .has_max = true, .max = 3};
    tui_sparkline_render(&a, &h, b.area, &b);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 0)), "█");
    tui_buffer_free(&b);
    tui_arena_free(&a);
    return mesh_check_done();
}
