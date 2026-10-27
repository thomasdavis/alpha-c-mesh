/* @ajax/tui-table: ratatui's Table, Row, Cell and TableState. Column widths come from a Layout of
 * the width constraints (Flex::Start, column_spacing between); rows scroll to keep the selected
 * one visible; the highlight symbol takes a column of its own while something is selected. */
#ifndef TUI_TABLE_H
#define TUI_TABLE_H

#include "tui/block.h"
#include "tui/layout.h"

typedef struct {
    tui_text content;
    tui_style style;
    uint16_t column_span; /* 1 */
} tui_table_cell;

typedef struct {
    tui_table_cell *cells;
    size_t n, cap;
    uint16_t height; /* 1 */
    uint16_t top_margin, bottom_margin;
    tui_style style;
} tui_table_row;

enum { TUI_HIGHLIGHT_ALWAYS, TUI_HIGHLIGHT_WHEN_SELECTED, TUI_HIGHLIGHT_NEVER };

typedef struct {
    const tui_table_row *rows;
    size_t nrows;
    const tui_table_row *header, *footer; /* optional */
    const tui_constraint *widths;
    size_t nwidths;
    uint16_t column_spacing; /* 1 */
    const tui_block *block;
    tui_style style, row_highlight_style, column_highlight_style, cell_highlight_style;
    tui_text highlight_symbol;
    int highlight_spacing; /* TUI_HIGHLIGHT_WHEN_SELECTED */
    tui_flex flex;         /* TUI_FLEX_START */
} tui_table;

typedef struct {
    size_t offset;
    bool has_selected, has_selected_column;
    size_t selected, selected_column;
} tui_table_state;

static inline tui_table_cell tui_table_cell_text(tui_text t) { return (tui_table_cell){.content = t, .column_span = 1}; }
static inline tui_table_row tui_table_row_new(void) {
    tui_table_row r = {0};
    r.height = 1;
    return r;
}
void tui_table_row_push(tui_arena *a, tui_table_row *r, tui_table_cell c);
static inline tui_table tui_table_new(const tui_table_row *rows, size_t nrows, const tui_constraint *widths, size_t nwidths) {
    tui_table t = {0};
    t.rows = rows;
    t.nrows = nrows;
    t.widths = widths;
    t.nwidths = nwidths;
    t.column_spacing = 1;
    t.highlight_symbol = tui_text_empty();
    t.highlight_spacing = TUI_HIGHLIGHT_WHEN_SELECTED;
    t.flex = TUI_FLEX_START;
    return t;
}

void tui_table_render(tui_arena *a, const tui_table *t, tui_rect area, tui_buffer *buf, tui_table_state *state);

#endif
