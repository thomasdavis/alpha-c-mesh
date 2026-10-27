/* A table as ratatui draws one: a header, Length and Fill columns with spacing, the highlight
 * symbol's column appearing with a selection, and scrolling to keep the selection visible. */
#include "mesh/check.h"
#include "tui/table.h"

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

static tui_table_row mk(tui_arena *a, const char *x, const char *y) {
    tui_table_row r = tui_table_row_new();
    tui_table_row_push(a, &r, tui_table_cell_text(tui_text_from_str(a, x)));
    tui_table_row_push(a, &r, tui_table_cell_text(tui_text_from_str(a, y)));
    return r;
}

int main(void) {
    tui_arena a = {0};
    tui_buffer b;
    tui_buffer_init(&b, (tui_rect){0, 0, 12, 3});
    tui_table_row rows[4] = {mk(&a, "a", "one"), mk(&a, "b", "two"), mk(&a, "c", "three"), mk(&a, "d", "four")};
    tui_table_row head = mk(&a, "k", "value");
    tui_constraint w[2] = {TUI_LENGTH(3), TUI_FILL(1)};
    tui_table t = tui_table_new(rows, 4, w, 2);
    t.header = &head;
    tui_table_render(&a, &t, b.area, &b, NULL);
    CHECK_S(row(&b, 0), "k   value   ");
    CHECK_S(row(&b, 1), "a   one     ");
    CHECK_S(row(&b, 2), "b   two     ");
    /* Select the last row: a highlight column, and the rows scroll. */
    tui_buffer_clear(&b, b.area);
    t.highlight_symbol = tui_text_from_str(&a, ">");
    t.row_highlight_style = tui_style_bold(tui_style_new());
    tui_table_state st = {.has_selected = true, .selected = 3};
    tui_table_render(&a, &t, b.area, &b, &st);
    CHECK_S(row(&b, 0), " k   value  ");
    CHECK_S(row(&b, 1), " c   three  ");
    CHECK_S(row(&b, 2), ">d   four   ");
    CHECK_U(st.offset, 2);
    CHECK_U(tui_buffer_cell(&b, 5, 2)->mod, TUI_BOLD);
    tui_buffer_free(&b);
    tui_arena_free(&a);
    tui_layout_cache_clear();
    return mesh_check_done();
}
