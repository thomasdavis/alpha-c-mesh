/* ratatui-widgets 0.3.2's Table (StatefulWidget for &Table), ported. */
#include "tui/table.h"

#include <stdlib.h>
#include <string.h>

void tui_table_row_push(tui_arena *a, tui_table_row *r, tui_table_cell c) {
    if (r->n == r->cap) {
        size_t cap = r->cap ? r->cap * 2 : 8;
        r->cells = tui_arena_grow(a, r->cells, r->cap * sizeof *r->cells, cap * sizeof *r->cells);
        r->cap = cap;
    }
    r->cells[r->n++] = c;
}

static uint16_t height_with_margin(const tui_table_row *r) {
    return tui_sat_add(tui_sat_add(r->height, r->top_margin), r->bottom_margin);
}

static void render_cell(tui_arena *a, const tui_table_cell *c, tui_rect area, tui_buffer *buf) {
    tui_buffer_set_style(buf, area, c->style);
    tui_text_render(a, &c->content, area, buf);
}

static void render_edge(tui_arena *a, const tui_table_row *r, tui_rect area, tui_buffer *buf, const tui_rect *cols, size_t ncols) {
    if (!r) return;
    tui_buffer_set_style(buf, area, r->style);
    for (size_t i = 0; i < ncols && i < r->n; i++) {
        tui_rect ca = tui_rect_new((uint16_t)(area.x + cols[i].x), area.y, cols[i].width, area.height);
        render_cell(a, &r->cells[i], ca, buf);
    }
}

static void visible_rows(const tui_table *t, const tui_table_state *st, tui_rect area, size_t *s, size_t *e) {
    size_t last = t->nrows ? t->nrows - 1 : 0;
    size_t start = st->offset < last ? st->offset : last;
    if (st->has_selected && st->selected < start) start = st->selected;
    size_t end = start;
    uint32_t height = 0;
    for (size_t i = start; i < t->nrows; i++) {
        if (height + t->rows[i].height > area.height) break;
        height += height_with_margin(&t->rows[i]);
        end++;
    }
    if (st->has_selected) {
        size_t sel = st->selected < last ? st->selected : last;
        while (sel >= end) {
            height += height_with_margin(&t->rows[end]);
            end++;
            while (height > area.height) {
                height = height > height_with_margin(&t->rows[start]) ? height - height_with_margin(&t->rows[start]) : 0;
                start++;
            }
        }
    }
    if (height < area.height && end < t->nrows) end++;
    *s = start;
    *e = end;
}

void tui_table_render(tui_arena *a, const tui_table *t, tui_rect area, tui_buffer *buf, tui_table_state *state) {
    tui_table_state local = {0};
    if (!state) state = &local;
    tui_buffer_set_style(buf, area, t->style);
    if (t->block) tui_block_render(a, t->block, area, buf);
    tui_rect ta = tui_block_inner_opt(t->block, area);
    if (tui_rect_empty(ta)) return;
    if (state->has_selected && state->selected >= t->nrows) state->selected = t->nrows ? t->nrows - 1 : 0;
    if (t->nrows == 0) state->has_selected = false;
    size_t ncols = 0;
    for (size_t i = 0; i < t->nrows; i++)
        if (t->rows[i].n > ncols) ncols = t->rows[i].n;
    if (t->footer && t->footer->n > ncols) ncols = t->footer->n;
    if (t->header && t->header->n > ncols) ncols = t->header->n;
    if (state->has_selected_column && state->selected_column >= ncols) state->selected_column = ncols ? ncols - 1 : 0;
    if (ncols == 0) state->has_selected_column = false;
    bool add_sel = t->highlight_spacing == TUI_HIGHLIGHT_ALWAYS ||
                   (t->highlight_spacing == TUI_HIGHLIGHT_WHEN_SELECTED && state->has_selected);
    uint16_t sel_w = add_sel ? (uint16_t)tui_text_width(&t->highlight_symbol) : 0;

    /* Column widths: the selection column, then the widths' layout. */
    size_t nw = t->nwidths;
    tui_constraint *widths = NULL;
    if (nw == 0) {
        nw = ncols;
        widths = tui_arena_alloc(a, (nw ? nw : 1) * sizeof *widths);
        for (size_t i = 0; i < nw; i++) widths[i] = TUI_LENGTH(ta.width / (ncols ? ncols : 1));
    } else {
        widths = tui_arena_alloc(a, nw * sizeof *widths);
        memcpy(widths, t->widths, nw * sizeof *widths);
    }
    tui_rect two[2];
    tui_hsplit((tui_rect){0, 0, ta.width, 1}, 2, (tui_constraint[]){TUI_LENGTH(sel_w), TUI_FILL(0)}, two);
    tui_rect *cols = tui_arena_alloc(a, (nw ? nw : 1) * sizeof *cols);
    tui_layout cl = {.vertical = false, .constraints = widths, .n = nw, .flex = t->flex, .spacing = (int16_t)t->column_spacing};
    tui_layout_split(&cl, two[1], cols, NULL);
    for (size_t i = 0; i < nw; i++) cols[i] = tui_rect_new(cols[i].x, 0, cols[i].width, 1);

    /* Header, rows, footer areas. */
    tui_rect parts[7];
    tui_constraint pc[7] = {
        TUI_LENGTH(t->header ? t->header->top_margin : 0), TUI_LENGTH(t->header ? t->header->height : 0),
        TUI_LENGTH(t->header ? t->header->bottom_margin : 0), TUI_MIN(0),
        TUI_LENGTH(t->footer ? t->footer->top_margin : 0), TUI_LENGTH(t->footer ? t->footer->height : 0),
        TUI_LENGTH(t->footer ? t->footer->bottom_margin : 0),
    };
    tui_vsplit(ta, 7, pc, parts);
    render_edge(a, t->header, parts[1], buf, cols, nw);

    tui_rect rows_area = parts[3];
    if (t->nrows) {
        size_t start, end;
        visible_rows(t, state, rows_area, &start, &end);
        state->offset = start;
        uint16_t y_off = 0;
        bool has_row_area = false;
        tui_rect sel_row = {0, 0, 0, 0};
        for (size_t i = start; i < end; i++) {
            const tui_table_row *r = &t->rows[i];
            uint16_t y = (uint16_t)(rows_area.y + y_off + r->top_margin);
            uint16_t h = tui_sat_sub(tui_min16((uint16_t)(y + r->height), tui_rect_bottom(rows_area)), y);
            tui_rect ra = {rows_area.x, y, rows_area.width, h};
            tui_buffer_set_style(buf, ra, r->style);
            bool is_sel = state->has_selected && state->selected == i;
            if (sel_w > 0 && is_sel) {
                tui_rect sa = ra;
                sa.width = sel_w;
                tui_buffer_set_style(buf, sa, r->style);
                tui_text_render(a, &t->highlight_symbol, sa, buf);
            }
            /* Cells, spanning columns. */
            size_t ci = 0;
            for (size_t k = 0; k < r->n; k++) {
                uint16_t span = r->cells[k].column_span;
                if (span == 0) continue;
                if (ci >= nw) break;
                tui_rect first = cols[ci++];
                uint16_t width = first.width;
                uint16_t taken = 1;
                for (uint16_t s = 1; s < span && ci < nw; s++, ci++) {
                    width = (uint16_t)(width + cols[ci].width);
                    taken++;
                }
                width = (uint16_t)(width + (taken - 1) * t->column_spacing);
                tui_rect ca = tui_rect_new((uint16_t)(ra.x + first.x), ra.y, width, ra.height);
                render_cell(a, &r->cells[k], ca, buf);
            }
            if (is_sel) {
                sel_row = ra;
                has_row_area = true;
            }
            y_off = (uint16_t)(y_off + height_with_margin(r));
        }
        bool has_col = state->has_selected_column && state->selected_column < nw;
        tui_rect col_area = {0, 0, 0, 0};
        if (has_col) col_area = (tui_rect){(uint16_t)(cols[state->selected_column].x + rows_area.x), rows_area.y,
                                           cols[state->selected_column].width, rows_area.height};
        if (has_row_area && has_col) {
            tui_buffer_set_style(buf, sel_row, t->row_highlight_style);
            tui_buffer_set_style(buf, col_area, t->column_highlight_style);
            tui_buffer_set_style(buf, tui_rect_intersection(sel_row, col_area), t->cell_highlight_style);
        } else if (has_row_area) {
            tui_buffer_set_style(buf, sel_row, t->row_highlight_style);
        } else if (has_col) {
            tui_buffer_set_style(buf, col_area, t->column_highlight_style);
        }
    }
    render_edge(a, t->footer, parts[5], buf, cols, nw);
}
