/* Blocks as ratatui draws them: rounded borders, a title over the top edge, inner areas. */
#include "mesh/check.h"
#include "tui/block.h"

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
    tui_buffer buf;
    tui_buffer_init(&buf, (tui_rect){0, 0, 12, 3});
    tui_block b = tui_block_bordered();
    b.set = &TUI_BORDER_ROUNDED;
    b.border_style = tui_fg(TUI_BLUE);
    tui_block_add_title(&a, &b, tui_line_from_str(&a, " hi "), TUI_TITLE_DEFAULT);
    tui_block_render(&a, &b, buf.area, &buf);
    CHECK_S(row(&buf, 0), "╭ hi ──────╮");
    CHECK_S(row(&buf, 1), "│          │");
    CHECK_S(row(&buf, 2), "╰──────────╯");
    CHECK(tui_color_eq(tui_buffer_cell(&buf, 0, 0)->fg, TUI_BLUE));
    CHECK(tui_color_eq(tui_buffer_cell(&buf, 1, 0)->fg, TUI_BLUE)); /* an unstyled title keeps the border colour under it */
    CHECK(tui_rect_eq(tui_block_inner(&b, buf.area), (tui_rect){1, 1, 10, 1}));
    /* A title alone still takes the top row. */
    tui_block t = tui_block_new();
    tui_block_add_title(&a, &t, tui_line_from_str(&a, "x"), TUI_TITLE_DEFAULT);
    CHECK(tui_rect_eq(tui_block_inner(&t, (tui_rect){0, 0, 5, 5}), (tui_rect){0, 1, 5, 4}));
    /* Right and centred titles. */
    tui_buffer_clear(&buf, buf.area);
    tui_block r = tui_block_bordered();
    tui_line rt = tui_line_from_str(&a, "R");
    rt.align = TUI_ALIGN_RIGHT;
    tui_line ct = tui_line_from_str(&a, "C");
    ct.align = TUI_ALIGN_CENTER;
    tui_block_add_title(&a, &r, rt, TUI_TITLE_DEFAULT);
    tui_block_add_title(&a, &r, ct, TUI_TITLE_BOTTOM);
    tui_block_render(&a, &r, buf.area, &buf);
    CHECK_S(row(&buf, 0), "┌─────────R┐");
    CHECK_S(row(&buf, 2), "└────C─────┘");
    tui_buffer_free(&buf);
    tui_arena_free(&a);
    return mesh_check_done();
}
