/* Lines and text as ratatui renders them: alignment, truncation of the left part when a right-
 * or centre-aligned line is too long, wide characters, styles. */
#include "mesh/check.h"
#include "tui/text.h"

#include <string.h>

static const char *row(const tui_buffer *b, uint16_t y) {
    static char out[512];
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
    tui_buffer_init(&b, (tui_rect){0, 0, 10, 4});
    tui_line l = tui_line_empty();
    tui_line_push(&a, &l, tui_span_raw("ab"));
    tui_line_push(&a, &l, tui_span_styled("cd", tui_fg(TUI_RED)));
    CHECK_U(tui_line_width(&l), 4);
    l.align = TUI_ALIGN_RIGHT;
    tui_line_render(&a, &l, (tui_rect){0, 0, 10, 1}, &b, TUI_ALIGN_NONE);
    CHECK_S(row(&b, 0), "      abcd");
    CHECK(tui_color_eq(tui_buffer_cell(&b, 9, 0)->fg, TUI_RED));
    l.align = TUI_ALIGN_CENTER;
    tui_line_render(&a, &l, (tui_rect){0, 1, 10, 1}, &b, TUI_ALIGN_NONE);
    CHECK_S(row(&b, 1), "   abcd   ");
    /* Too long: right alignment keeps the end, centre the middle. */
    tui_line long_l = tui_line_from_str(&a, "0123456789abcdef");
    long_l.align = TUI_ALIGN_RIGHT;
    tui_line_render(&a, &long_l, (tui_rect){0, 2, 10, 1}, &b, TUI_ALIGN_NONE);
    CHECK_S(row(&b, 2), "6789abcdef");
    long_l.align = TUI_ALIGN_CENTER;
    tui_line_render(&a, &long_l, (tui_rect){0, 3, 10, 1}, &b, TUI_ALIGN_NONE);
    CHECK_S(row(&b, 3), "3456789abc");
    /* str::lines semantics. */
    tui_text t = tui_text_from_str(&a, "one\r\ntwo\n\nfour\n");
    CHECK_U(t.n, 4);
    CHECK_S(t.lines[1].spans[0].s, "two");
    CHECK_U(t.lines[2].spans[0].s[0], 0);
    CHECK_U(tui_text_from_str(&a, "").n, 0);
    CHECK_U(tui_line_from_str(&a, "a\nb").n, 2);
    /* Wide characters in a right-aligned overflow: the cut falls between graphemes. */
    tui_buffer_clear(&b, b.area);
    tui_line w = tui_line_from_str(&a, "\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5\xE6\x97\xA5");
    w.align = TUI_ALIGN_RIGHT;
    tui_line_render(&a, &w, (tui_rect){0, 0, 9, 1}, &b, TUI_ALIGN_NONE);
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 1, 0)), "\xE6\x97\xA5");
    CHECK_S(tui_cell_symbol(tui_buffer_cell(&b, 0, 0)), " ");
    /* set_line patches the line's style with each span's. */
    tui_line s = tui_line_empty();
    s.style = tui_fg(TUI_BLUE);
    tui_line_push(&a, &s, tui_span_styled("x", tui_style_bold(tui_style_new())));
    uint16_t x = tui_buffer_set_line(&b, 0, 3, &s, 5);
    CHECK_U(x, 1);
    CHECK(tui_color_eq(tui_buffer_cell(&b, 0, 3)->fg, TUI_BLUE));
    CHECK_U(tui_buffer_cell(&b, 0, 3)->mod, TUI_BOLD);
    tui_sgrapheme *g;
    CHECK_U(tui_line_graphemes(&a, &l, tui_style_new(), &g), 4);
    tui_buffer_free(&b);
    tui_arena_free(&a);
    return mesh_check_done();
}
