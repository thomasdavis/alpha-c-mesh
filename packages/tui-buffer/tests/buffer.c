/* Buffer::set_stringn's rules from ratatui's tests: truncation at the edge and at max_width, wide
 * graphemes resetting the cell they cover, control characters skipped, styles patched. */
#include "mesh/check.h"
#include "tui/buffer.h"

#include <string.h>

static const char *row(const tui_buffer *b, uint16_t y) {
    static char out[256];
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
    tui_buffer b;
    tui_buffer_init(&b, (tui_rect){0, 0, 5, 2});
    CHECK_S(row(&b, 0), "     ");
    tui_buffer_set_string(&b, 0, 0, "hello world", tui_fg(TUI_RED));
    CHECK_S(row(&b, 0), "hello");
    CHECK(tui_color_eq(tui_buffer_cell(&b, 4, 0)->fg, TUI_RED));
    tui_buffer_set_stringn(&b, 0, 1, "abcdef", 6, 2, tui_style_new());
    CHECK_S(row(&b, 1), "ab   ");
    /* A wide grapheme that doesn't fit is not written. */
    tui_buffer_set_string(&b, 3, 1, "x\xE6\x97\xA5", tui_style_new());
    CHECK_S(row(&b, 1), "ab x ");
    /* A wide grapheme resets the cell it covers. */
    tui_buffer_set_string(&b, 0, 1, "\xE6\x97\xA5", tui_style_new());
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 1)), "\xE6\x97\xA5");
    CHECK(tui_buffer_cell(&b, 1, 1)->none);
    /* Control characters vanish; zero-width graphemes too. */
    tui_buffer_clear(&b, b.area);
    uint16_t x = tui_buffer_set_string(&b, 0, 0, "a\tb\n", tui_style_new());
    CHECK_U(x, 2);
    CHECK_S(row(&b, 0), "ab   ");
    /* set_style patches. */
    tui_buffer_set_style(&b, (tui_rect){1, 0, 10, 1}, tui_style_add(tui_fg(TUI_BLUE), TUI_BOLD));
    CHECK(tui_color_eq(tui_buffer_cell(&b, 1, 0)->fg, TUI_BLUE));
    CHECK_U(tui_buffer_cell(&b, 1, 0)->mod, TUI_BOLD);
    CHECK(tui_color_eq(tui_buffer_cell(&b, 0, 0)->fg, TUI_RESET));
    CHECK(tui_buffer_cell(&b, 5, 0) == NULL);
    CHECK_U(tui_symbol_width("\xEF\xBD\xB1\xEF\xBE\x9E", 6), 2);
    tui_cell c = TUI_CELL_EMPTY;
    tui_cell_append_symbol(&c, "\xCC\x81", 2);
    CHECK_S(tui_cell_symbol(&c), "\xCC\x81");
    tui_buffer_free(&b);
    return mesh_check_done();
}
