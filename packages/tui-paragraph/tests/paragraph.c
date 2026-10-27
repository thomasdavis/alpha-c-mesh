/* Paragraphs as ratatui's reflow tests describe them: word wrapping with and without trimming,
 * long words broken, truncation, alignment, scrolling, a block around it. */
#include "mesh/check.h"
#include "tui/paragraph.h"

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

static void draw(tui_arena *a, tui_buffer *b, const char *text, int wrap, int8_t align, uint16_t scroll) {
    tui_buffer_clear(b, b->area);
    tui_paragraph p = tui_paragraph_new(tui_text_from_str(a, text));
    p.wrap = wrap;
    p.alignment = align;
    p.scroll_y = scroll;
    tui_paragraph_render(a, &p, b->area, b);
}

int main(void) {
    tui_arena a = {0};
    tui_buffer b;
    tui_buffer_init(&b, (tui_rect){0, 0, 10, 4});
    draw(&a, &b, "The quick brown fox jumps", TUI_WRAP_TRIM, TUI_ALIGN_LEFT, 0);
    CHECK_S(row(&b, 0), "The quick ");
    CHECK_S(row(&b, 1), "brown fox ");
    CHECK_S(row(&b, 2), "jumps     ");
    draw(&a, &b, "abcdefghijklmnop", TUI_WRAP_TRIM, TUI_ALIGN_LEFT, 0);
    CHECK_S(row(&b, 0), "abcdefghij");
    CHECK_S(row(&b, 1), "klmnop    ");
    draw(&a, &b, "  indented words here", TUI_WRAP_KEEP, TUI_ALIGN_LEFT, 0);
    CHECK_S(row(&b, 0), "  indented");
    draw(&a, &b, "  indented words here", TUI_WRAP_TRIM, TUI_ALIGN_LEFT, 0); /* trim drops the indent */
    CHECK_S(row(&b, 0), "indented  ");
    CHECK_S(row(&b, 1), "words here");
    draw(&a, &b, "a long line cut here\nsecond", TUI_WRAP_NONE, TUI_ALIGN_LEFT, 0);
    CHECK_S(row(&b, 0), "a long lin");
    CHECK_S(row(&b, 1), "second    ");
    draw(&a, &b, "mid", TUI_WRAP_NONE, TUI_ALIGN_CENTER, 0);
    CHECK_S(row(&b, 0), "    mid   ");
    draw(&a, &b, "r", TUI_WRAP_NONE, TUI_ALIGN_RIGHT, 0);
    CHECK_S(row(&b, 0), "         r");
    draw(&a, &b, "one\ntwo\nthree", TUI_WRAP_TRIM, TUI_ALIGN_LEFT, 1);
    CHECK_S(row(&b, 0), "two       ");
    /* In a block. */
    tui_buffer_clear(&b, b.area);
    tui_block blk = tui_block_bordered();
    tui_paragraph p = tui_paragraph_new(tui_text_from_str(&a, "inside the box"));
    p.block = &blk;
    p.wrap = TUI_WRAP_TRIM;
    tui_paragraph_render(&a, &p, b.area, &b);
    CHECK_S(row(&b, 0), "┌────────┐");
    CHECK_S(row(&b, 1), "│inside  │");
    CHECK_S(row(&b, 2), "│the box │");
    tui_buffer_free(&b);
    tui_arena_free(&a);
    return mesh_check_done();
}
