/* ratatui-widgets 0.3.2's Block: sides, then corners, then titles (left, centre, right per edge). */
#include "tui/block.h"

#include <string.h>

const tui_border_set TUI_BORDER_PLAIN = {"┌", "┐", "└", "┘", "│", "│", "─", "─"};
const tui_border_set TUI_BORDER_ROUNDED = {"╭", "╮", "╰", "╯", "│", "│", "─", "─"};
const tui_border_set TUI_BORDER_DOUBLE = {"╔", "╗", "╚", "╝", "║", "║", "═", "═"};
const tui_border_set TUI_BORDER_THICK = {"┏", "┓", "┗", "┛", "┃", "┃", "━", "━"};

void tui_block_add_title(tui_arena *a, tui_block *b, tui_line line, int8_t position) {
    if (b->ntitles == b->captitles) {
        size_t cap = b->captitles ? b->captitles * 2 : 2;
        b->titles = tui_arena_grow(a, b->titles, b->captitles * sizeof *b->titles, cap * sizeof *b->titles);
        b->captitles = cap;
    }
    b->titles[b->ntitles++] = (tui_block_title){line, position};
}

static int8_t pos_of(const tui_block *b, const tui_block_title *t) {
    return t->position == TUI_TITLE_DEFAULT ? b->titles_position : t->position;
}

static bool has_title_at(const tui_block *b, int8_t pos) {
    for (size_t i = 0; i < b->ntitles; i++)
        if (pos_of(b, &b->titles[i]) == pos) return true;
    return false;
}

tui_rect tui_block_inner(const tui_block *b, tui_rect area) {
    tui_rect r = area;
    if (b->borders & TUI_BORDER_LEFT) {
        r.x = tui_min16(tui_sat_add(r.x, 1), tui_rect_right(r));
        r.width = tui_sat_sub(r.width, 1);
    }
    if ((b->borders & TUI_BORDER_TOP) || has_title_at(b, TUI_TITLE_TOP)) {
        r.y = tui_min16(tui_sat_add(r.y, 1), tui_rect_bottom(r));
        r.height = tui_sat_sub(r.height, 1);
    }
    if (b->borders & TUI_BORDER_RIGHT) r.width = tui_sat_sub(r.width, 1);
    if ((b->borders & TUI_BORDER_BOTTOM) || has_title_at(b, TUI_TITLE_BOTTOM)) r.height = tui_sat_sub(r.height, 1);
    r.x = tui_sat_add(r.x, b->pad_left);
    r.y = tui_sat_add(r.y, b->pad_top);
    r.width = tui_sat_sub(r.width, tui_sat_add(b->pad_left, b->pad_right));
    r.height = tui_sat_sub(r.height, tui_sat_add(b->pad_top, b->pad_bottom));
    return r;
}

static void put(tui_buffer *buf, uint16_t x, uint16_t y, const char *sym, tui_style st) {
    tui_cell *c = tui_buffer_cell(buf, x, y);
    if (!c) return;
    tui_cell_set_str(c, sym);
    tui_cell_set_style(c, st);
}

static void render_borders(const tui_block *b, tui_rect area, tui_buffer *buf) {
    const tui_border_set *s = b->set ? b->set : &TUI_BORDER_PLAIN;
    uint16_t left = area.x, top = area.y;
    uint16_t right = tui_sat_sub(tui_rect_right(area), 1), bottom = tui_sat_sub(tui_rect_bottom(area), 1);
    if (b->borders & TUI_BORDER_LEFT)
        for (uint16_t y = top; y <= bottom; y++) put(buf, left, y, s->vertical_left, b->border_style);
    if (b->borders & TUI_BORDER_TOP)
        for (uint16_t x = left; x <= right; x++) put(buf, x, top, s->horizontal_top, b->border_style);
    if (b->borders & TUI_BORDER_RIGHT)
        for (uint16_t y = top; y <= bottom; y++) put(buf, right, y, s->vertical_right, b->border_style);
    if (b->borders & TUI_BORDER_BOTTOM)
        for (uint16_t x = left; x <= right; x++) put(buf, x, bottom, s->horizontal_bottom, b->border_style);
    if ((b->borders & (TUI_BORDER_RIGHT | TUI_BORDER_BOTTOM)) == (TUI_BORDER_RIGHT | TUI_BORDER_BOTTOM))
        put(buf, right, bottom, s->bottom_right, b->border_style);
    if ((b->borders & (TUI_BORDER_RIGHT | TUI_BORDER_TOP)) == (TUI_BORDER_RIGHT | TUI_BORDER_TOP))
        put(buf, right, top, s->top_right, b->border_style);
    if ((b->borders & (TUI_BORDER_LEFT | TUI_BORDER_BOTTOM)) == (TUI_BORDER_LEFT | TUI_BORDER_BOTTOM))
        put(buf, left, bottom, s->bottom_left, b->border_style);
    if ((b->borders & (TUI_BORDER_LEFT | TUI_BORDER_TOP)) == (TUI_BORDER_LEFT | TUI_BORDER_TOP))
        put(buf, left, top, s->top_left, b->border_style);
}

static tui_rect titles_area(const tui_block *b, tui_rect area, int8_t pos) {
    uint16_t lb = (b->borders & TUI_BORDER_LEFT) ? 1 : 0, rb = (b->borders & TUI_BORDER_RIGHT) ? 1 : 0;
    return (tui_rect){tui_sat_add(area.x, lb), pos == TUI_TITLE_TOP ? area.y : tui_sat_sub(tui_rect_bottom(area), 1),
                      tui_sat_sub(tui_sat_sub(area.width, lb), rb), 1};
}

static uint16_t lw(const tui_line *l) {
    size_t w = tui_line_width(l);
    return (uint16_t)(w > 0xFFFF ? 0xFFFF : w);
}

static bool matches(const tui_block *b, const tui_block_title *t, int8_t pos, int8_t align) {
    int8_t a = t->line.align != TUI_ALIGN_NONE ? t->line.align : b->titles_alignment;
    return pos_of(b, t) == pos && a == align;
}

static void left_titles(tui_arena *a, const tui_block *b, int8_t pos, tui_rect area, tui_buffer *buf) {
    tui_rect ta = titles_area(b, area, pos);
    for (size_t i = 0; i < b->ntitles; i++) {
        if (!matches(b, &b->titles[i], pos, TUI_ALIGN_LEFT)) continue;
        if (tui_rect_empty(ta)) break;
        uint16_t w = lw(&b->titles[i].line);
        tui_rect r = ta;
        r.width = tui_min16(w, ta.width);
        tui_buffer_set_style(buf, r, b->titles_style);
        tui_line_render(a, &b->titles[i].line, r, buf, TUI_ALIGN_NONE);
        uint16_t adv = tui_sat_add(w, 1);
        ta.x = tui_sat_add(ta.x, adv);
        ta.width = tui_sat_sub(ta.width, adv);
    }
}

static void right_titles(tui_arena *a, const tui_block *b, int8_t pos, tui_rect area, tui_buffer *buf) {
    tui_rect ta = titles_area(b, area, pos);
    for (size_t k = b->ntitles; k-- > 0;) {
        if (!matches(b, &b->titles[k], pos, TUI_ALIGN_RIGHT)) continue;
        if (tui_rect_empty(ta)) break;
        uint16_t w = lw(&b->titles[k].line);
        tui_rect r = ta;
        r.x = tui_max16(tui_sat_sub(tui_rect_right(ta), w), ta.x);
        r.width = tui_min16(w, ta.width);
        tui_buffer_set_style(buf, r, b->titles_style);
        tui_line_render(a, &b->titles[k].line, r, buf, TUI_ALIGN_NONE);
        ta.width = tui_sat_sub(tui_sat_sub(ta.width, w), 1);
    }
}

static void center_titles(tui_arena *a, const tui_block *b, int8_t pos, tui_rect area, tui_buffer *buf) {
    tui_rect ta = titles_area(b, area, pos);
    uint16_t total = 0;
    size_t count = 0;
    for (size_t i = 0; i < b->ntitles; i++)
        if (matches(b, &b->titles[i], pos, TUI_ALIGN_CENTER)) {
            total = tui_sat_add(total, tui_sat_add(lw(&b->titles[i].line), 1));
            count++;
        }
    if (!count) return;
    total = tui_sat_sub(total, 1);
    if (total <= ta.width) {
        uint16_t x = tui_sat_add(ta.x, (uint16_t)(tui_sat_sub(ta.width, total) / 2));
        tui_rect r = ta;
        r.x = x;
        for (size_t i = 0; i < b->ntitles; i++) {
            if (!matches(b, &b->titles[i], pos, TUI_ALIGN_CENTER)) continue;
            uint16_t w = lw(&b->titles[i].line);
            tui_rect t = r;
            t.width = w;
            tui_buffer_set_style(buf, t, b->titles_style);
            tui_line_render(a, &b->titles[i].line, t, buf, TUI_ALIGN_NONE);
            uint16_t adv = tui_sat_add(w, 1);
            r.x = tui_sat_add(r.x, adv);
            r.width = tui_sat_sub(r.width, adv);
        }
    } else {
        uint16_t offset = (uint16_t)(tui_sat_sub(total, ta.width) / 2);
        for (size_t i = 0; i < b->ntitles; i++) {
            if (!matches(b, &b->titles[i], pos, TUI_ALIGN_CENTER)) continue;
            if (tui_rect_empty(ta)) break;
            uint16_t w = tui_sat_sub(tui_min16(ta.width, lw(&b->titles[i].line)), offset);
            tui_rect t = ta;
            t.width = w;
            tui_buffer_set_style(buf, t, b->titles_style);
            tui_line l = b->titles[i].line;
            if (offset > 0) {
                l.align = TUI_ALIGN_RIGHT;
                tui_line_render(a, &l, t, buf, TUI_ALIGN_NONE);
                offset = tui_sat_sub(tui_sat_sub(offset, w), 1);
            } else {
                l.align = TUI_ALIGN_LEFT;
                tui_line_render(a, &l, t, buf, TUI_ALIGN_NONE);
            }
            uint16_t adv = tui_sat_add(w, 1);
            ta.x = tui_sat_add(ta.x, adv);
            ta.width = tui_sat_sub(ta.width, adv);
        }
    }
}

void tui_block_render(tui_arena *a, const tui_block *b, tui_rect area, tui_buffer *buf) {
    area = tui_rect_intersection(area, buf->area);
    if (tui_rect_empty(area)) return;
    tui_buffer_set_style(buf, area, b->style);
    render_borders(b, area, buf);
    for (int8_t pos = TUI_TITLE_TOP; pos <= TUI_TITLE_BOTTOM; pos++) {
        left_titles(a, b, pos, area, buf);
        center_titles(a, b, pos, area, buf);
        right_titles(a, b, pos, area, buf);
    }
}
