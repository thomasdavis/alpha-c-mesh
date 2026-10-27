/* The diff: unchanged cells are not written, contiguous cells don't move the cursor, colours and
 * modifiers change only when they do, a wide character's covered cell is skipped. */
#include "mesh/check.h"
#include "tui/render.h"

#include <string.h>

int main(void) {
    tui_buffer a, b;
    tui_buffer_init(&a, (tui_rect){0, 0, 6, 2});
    tui_buffer_init(&b, (tui_rect){0, 0, 6, 2});
    tui_out o = {0};
    tui_render_diff(&a, &b, &o);
    CHECK_S(o.data, "\x1b[39m\x1b[49m\x1b[0m");
    tui_out_free(&o);
    tui_buffer_set_string(&b, 1, 0, "ab", tui_fg(TUI_RGB(1, 2, 3)));
    tui_buffer_set_string(&b, 0, 1, "\xE6\x97\xA5x", tui_style_add(tui_style_new(), TUI_BOLD));
    tui_render_diff(&a, &b, &o);
    CHECK_S(o.data, "\x1b[1;2H\x1b[38;2;1;2;3m\x1b[49ma" "b"
                    "\x1b[2;1H\x1b[1m\x1b[39m\x1b[49m\xE6\x97\xA5"
                    "\x1b[2;3Hx"
                    "\x1b[39m\x1b[49m\x1b[0m");
    tui_out_free(&o);
    tui_buffer_free(&a);
    tui_buffer_free(&b);
    return mesh_check_done();
}
