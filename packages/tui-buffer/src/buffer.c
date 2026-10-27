/* Cells and the buffer. */
#include "tui/buffer.h"

#include "tui/grapheme.h"
#include "tui/unicode_width.h"
#include "tui/utf8.h"

#include <stdlib.h>
#include <string.h>

const tui_cell TUI_CELL_EMPTY = {.none = true};

const char *tui_cell_symbol(const tui_cell *c) { return c->none ? " " : c->sym; }

static size_t fit(const char *s, size_t len, size_t room) {
    if (len <= room) return len;
    /* Cut at a character boundary. */
    while (room > 0 && ((unsigned char)s[room] & 0xC0) == 0x80) room--;
    return room;
}

void tui_cell_set_symbol(tui_cell *c, const char *s, size_t len) {
    len = fit(s, len, TUI_SYM_MAX - 1);
    memcpy(c->sym, s, len);
    c->sym[len] = 0;
    c->none = false;
}

void tui_cell_set_str(tui_cell *c, const char *s) { tui_cell_set_symbol(c, s, strlen(s)); }

void tui_cell_append_symbol(tui_cell *c, const char *s, size_t len) {
    if (c->none) {
        c->sym[0] = 0;
        c->none = false;
    }
    size_t have = strlen(c->sym);
    len = fit(s, len, TUI_SYM_MAX - 1 - have);
    memcpy(c->sym + have, s, len);
    c->sym[have + len] = 0;
}

void tui_cell_set_style(tui_cell *c, tui_style st) {
    if (st.has_fg) c->fg = st.fg;
    if (st.has_bg) c->bg = st.bg;
    c->mod = (uint16_t)(c->mod | st.add);
    c->mod = (uint16_t)(c->mod & ~st.sub);
}

void tui_cell_reset(tui_cell *c) { *c = TUI_CELL_EMPTY; }

tui_style tui_cell_style(const tui_cell *c) {
    return (tui_style){.fg = c->fg, .bg = c->bg, .has_fg = true, .has_bg = true, .add = c->mod};
}

uint16_t tui_symbol_width(const char *s, size_t len) {
    if (len == 1) return 1;
    size_t w = tui_str_width(s, len);
    for (size_t i = 0; i < len;) {
        uint32_t cp = tui_utf8_next(s, len, &i);
        if (cp == 0xFF9E || cp == 0xFF9F) w++;
    }
    return (uint16_t)(w > 0xFFFF ? 0xFFFF : w);
}

bool tui_symbol_has_control(const char *s, size_t len) {
    for (size_t i = 0; i < len;) {
        uint32_t cp = tui_utf8_next(s, len, &i);
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) return true;
    }
    return false;
}

void tui_buffer_init(tui_buffer *b, tui_rect area) {
    size_t n = tui_rect_area(area);
    b->area = area;
    b->cells = malloc((n ? n : 1) * sizeof *b->cells);
    if (!b->cells) abort();
    for (size_t i = 0; i < n; i++) b->cells[i] = TUI_CELL_EMPTY;
}

void tui_buffer_free(tui_buffer *b) {
    free(b->cells);
    b->cells = NULL;
}

tui_cell *tui_buffer_cell(tui_buffer *b, uint16_t x, uint16_t y) {
    if (!tui_rect_contains(b->area, x, y)) return NULL;
    return &b->cells[(size_t)(y - b->area.y) * b->area.width + (x - b->area.x)];
}

const tui_cell *tui_buffer_cell_c(const tui_buffer *b, uint16_t x, uint16_t y) {
    return tui_buffer_cell((tui_buffer *)b, x, y);
}

uint16_t tui_buffer_set_stringn(tui_buffer *b, uint16_t x, uint16_t y, const char *s, size_t len, size_t max_width,
                                tui_style st) {
    uint16_t maxw = max_width > 0xFFFF ? 0xFFFF : (uint16_t)max_width;
    uint16_t remaining = tui_min16(tui_sat_sub(tui_rect_right(b->area), x), maxw);
    for (size_t i = 0; i < len;) {
        size_t end = tui_grapheme_next(s, len, i);
        const char *g = s + i;
        size_t glen = end - i;
        i = end;
        if (tui_symbol_has_control(g, glen)) continue;
        uint16_t w = tui_symbol_width(g, glen);
        if (w == 0) continue;
        if (w > remaining) break;
        remaining = (uint16_t)(remaining - w);
        tui_cell *c = tui_buffer_cell(b, x, y);
        if (c) {
            tui_cell_set_symbol(c, g, glen);
            tui_cell_set_style(c, st);
        }
        uint16_t next = (uint16_t)(x + w);
        x++;
        while (x < next) {
            tui_cell *h = tui_buffer_cell(b, x, y);
            if (h) tui_cell_reset(h);
            x++;
        }
    }
    return x;
}

uint16_t tui_buffer_set_string(tui_buffer *b, uint16_t x, uint16_t y, const char *s, tui_style st) {
    return tui_buffer_set_stringn(b, x, y, s, strlen(s), (size_t)-1, st);
}

void tui_buffer_set_style(tui_buffer *b, tui_rect area, tui_style st) {
    tui_rect r = tui_rect_intersection(b->area, area);
    for (uint16_t y = r.y; y < tui_rect_bottom(r); y++)
        for (uint16_t x = r.x; x < tui_rect_right(r); x++) tui_cell_set_style(tui_buffer_cell(b, x, y), st);
}

void tui_buffer_clear(tui_buffer *b, tui_rect area) {
    tui_rect r = tui_rect_intersection(area, b->area);
    if (tui_rect_empty(r)) return;
    for (uint16_t x = r.x; x < tui_rect_right(r); x++)
        for (uint16_t y = r.y; y < tui_rect_bottom(r); y++) tui_cell_reset(tui_buffer_cell(b, x, y));
}
