/* The splits mesh-top relies on most: lengths and fills, percentages, overflow. */
#include "mesh/check.h"
#include "tui/layout.h"

int main(void) {
    tui_rect out[3];
    tui_vsplit((tui_rect){0, 0, 100, 30}, 3, (tui_constraint[]){TUI_LENGTH(1), TUI_FILL(1), TUI_LENGTH(1)}, out);
    CHECK(tui_rect_eq(out[0], (tui_rect){0, 0, 100, 1}));
    CHECK(tui_rect_eq(out[1], (tui_rect){0, 1, 100, 28}));
    CHECK(tui_rect_eq(out[2], (tui_rect){0, 29, 100, 1}));
    tui_hsplit((tui_rect){0, 0, 100, 1}, 2, (tui_constraint[]){TUI_PERCENTAGE(45), TUI_FILL(1)}, out);
    CHECK_U(out[0].width, 45);
    CHECK_U(out[1].width, 55);
    tui_hsplit((tui_rect){0, 0, 99, 1}, 2, (tui_constraint[]){TUI_FILL(1), TUI_FILL(1)}, out);
    CHECK_U(out[0].width + out[1].width, 99);
    tui_layout_cache_clear();
    return mesh_check_done();
}
