/* Rect arithmetic as ratatui's Rect tests specify it, saturation included. */
#include "mesh/check.h"
#include "tui/rect.h"

int main(void) {
    tui_rect r = tui_rect_new(65530, 65530, 10, 10);
    CHECK_U(r.width, 5);
    CHECK_U(tui_rect_right(r), 65535);
    tui_rect a = {1, 2, 3, 4}, b = {2, 3, 4, 5};
    tui_rect i = tui_rect_intersection(a, b);
    CHECK(tui_rect_eq(i, (tui_rect){2, 3, 2, 3}));
    CHECK(tui_rect_eq(tui_rect_union(a, b), (tui_rect){1, 2, 5, 6}));
    CHECK(tui_rect_empty(tui_rect_intersection((tui_rect){0, 0, 2, 2}, (tui_rect){5, 5, 2, 2})));
    CHECK(tui_rect_eq(tui_rect_inner((tui_rect){0, 0, 10, 10}, 1, 2), (tui_rect){1, 2, 8, 6}));
    CHECK(tui_rect_eq(tui_rect_inner((tui_rect){0, 0, 1, 10}, 1, 0), (tui_rect){0, 0, 0, 0}));
    CHECK(tui_rect_eq(tui_rect_indent_x((tui_rect){3, 0, 5, 1}, 7), (tui_rect){10, 0, 0, 1}));
    CHECK(tui_rect_contains(a, 1, 2) && !tui_rect_contains(a, 4, 2));
    CHECK_U(tui_rect_area((tui_rect){0, 0, 300, 300}), 90000);
    return mesh_check_done();
}
