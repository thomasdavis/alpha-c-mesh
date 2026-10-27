/* ratatui-widgets 0.3.2's Paragraph with reflow.rs's WordWrapper and LineTruncator, ported. */
#include "tui/paragraph.h"

#include "tui/grapheme.h"
#include "tui/utf8.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    tui_sgrapheme *g;
    size_t n, cap;
} gvec;

static void gpush(tui_arena *a, gvec *v, tui_sgrapheme g) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 32;
        v->g = tui_arena_grow(a, v->g, v->cap * sizeof *v->g, cap * sizeof *v->g);
        v->cap = cap;
    }
    v->g[v->n++] = g;
}

static uint16_t gw(const tui_sgrapheme *g) { return tui_symbol_width(g->s, g->len); }

/* StyledGrapheme::is_whitespace: ZWSP, or all whitespace and not NBSP. */
static bool is_ws_cp(uint32_t c) {
    return (c >= 0x9 && c <= 0xD) || c == 0x20 || c == 0x85 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
           c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000;
}
static bool is_whitespace(const tui_sgrapheme *g) {
    if (g->len == 3 && memcmp(g->s, "\xE2\x80\x8B", 3) == 0) return true;
    if (g->len == 2 && memcmp(g->s, "\xC2\xA0", 2) == 0) return false;
    for (size_t i = 0; i < g->len;) {
        uint32_t c = tui_utf8_next(g->s, g->len, &i);
        if (!is_ws_cp(c)) return false;
    }
    return true;
}

typedef struct {
    gvec *lines;
    size_t n, cap;
} lvec;

static void lpush(tui_arena *a, lvec *v, gvec g) {
    if (v->n == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 16;
        v->lines = tui_arena_grow(a, v->lines, v->cap * sizeof *v->lines, cap * sizeof *v->lines);
        v->cap = cap;
    }
    v->lines[v->n++] = g;
}

/* WordWrapper::process_input for one input line: appends its wrapped lines to out. */
static void wrap_line(tui_arena *a, const tui_sgrapheme *in, size_t nin, uint16_t max, bool trim, lvec *out) {
    gvec pending = {0}, word = {0}, ws = {0}; /* ws is a deque: [ws_head, ws.n) */
    size_t ws_head = 0;
    uint16_t line_width = 0, word_width = 0, ws_width = 0;
    bool non_ws_prev = false;
    size_t before = out->n;
    for (size_t i = 0; i < nin; i++) {
        tui_sgrapheme g = in[i];
        bool is_ws = is_whitespace(&g);
        uint16_t sw = gw(&g);
        if (sw > max) continue;
        bool word_found = non_ws_prev && is_ws;
        bool trimmed_overflow = pending.n == 0 && trim && word_width + sw > max;
        bool ws_overflow = pending.n == 0 && trim && ws_width + sw > max;
        bool untrimmed_overflow = pending.n == 0 && !trim && word_width + ws_width + sw > max;
        if (word_found || trimmed_overflow || ws_overflow || untrimmed_overflow) {
            if (pending.n != 0 || !trim) {
                for (size_t k = ws_head; k < ws.n; k++) gpush(a, &pending, ws.g[k]);
                line_width = (uint16_t)(line_width + ws_width);
            }
            for (size_t k = 0; k < word.n; k++) gpush(a, &pending, word.g[k]);
            word.n = 0;
            line_width = (uint16_t)(line_width + word_width);
            ws.n = ws_head = 0;
            ws_width = 0;
            word_width = 0;
        }
        bool line_full = line_width >= max;
        bool word_overflow = sw > 0 && line_width + ws_width + word_width >= max;
        if (line_full || word_overflow) {
            uint16_t remaining = tui_sat_sub(max, line_width);
            lpush(a, out, pending);
            pending = (gvec){0};
            line_width = 0;
            while (ws_head < ws.n) {
                uint16_t w = gw(&ws.g[ws_head]);
                if (w > remaining) break;
                ws_width = (uint16_t)(ws_width - w);
                remaining = (uint16_t)(remaining - w);
                ws_head++;
            }
            if (is_ws && ws_head == ws.n) continue;
        }
        if (is_ws) {
            ws_width = (uint16_t)(ws_width + sw);
            gpush(a, &ws, g);
        } else {
            word_width = (uint16_t)(word_width + sw);
            gpush(a, &word, g);
        }
        non_ws_prev = !is_ws;
    }
    if (pending.n == 0 && word.n == 0 && ws_head < ws.n && trim) lpush(a, out, (gvec){0});
    if (pending.n != 0 || !trim)
        for (size_t k = ws_head; k < ws.n; k++) gpush(a, &pending, ws.g[k]);
    for (size_t k = 0; k < word.n; k++) gpush(a, &pending, word.g[k]);
    if (pending.n != 0) lpush(a, out, pending);
    if (out->n == before) lpush(a, out, (gvec){0});
}

static uint16_t line_offset(uint16_t lw, uint16_t aw, int8_t align) {
    if (align == TUI_ALIGN_CENTER) return tui_sat_sub((uint16_t)(aw / 2), (uint16_t)(lw / 2));
    if (align == TUI_ALIGN_RIGHT) return tui_sat_sub(aw, lw);
    return 0;
}

static void render_line(const tui_sgrapheme *g, size_t n, uint16_t width, int8_t align, tui_rect area, tui_buffer *buf,
                        uint16_t y) {
    uint16_t x = line_offset(width, area.width, align);
    for (size_t i = 0; i < n; i++) {
        uint16_t w = gw(&g[i]);
        if (w == 0) continue;
        tui_cell *c = tui_buffer_cell(buf, (uint16_t)(area.x + x), (uint16_t)(area.y + y));
        if (c) {
            if (g[i].len == 0) tui_cell_set_str(c, " ");
            else tui_cell_set_symbol(c, g[i].s, g[i].len);
            tui_cell_set_style(c, g[i].style);
        }
        x = (uint16_t)(x + w);
    }
}

void tui_paragraph_render(tui_arena *a, const tui_paragraph *p, tui_rect area, tui_buffer *buf) {
    area = tui_rect_intersection(area, buf->area);
    tui_buffer_set_style(buf, area, p->style);
    if (p->block) tui_block_render(a, p->block, area, buf);
    tui_rect ta = tui_block_inner_opt(p->block, area);
    if (tui_rect_empty(ta)) return;
    tui_buffer_set_style(buf, ta, p->style);
    uint16_t y = 0;
    if (p->wrap != TUI_WRAP_NONE) {
        /* Every wrapped line, then skip scroll_y of them. */
        uint16_t skip = p->scroll_y;
        for (size_t li = 0; li < p->text.n; li++) {
            const tui_line *l = &p->text.lines[li];
            tui_sgrapheme *g;
            size_t n = tui_line_graphemes(a, l, p->text.style, &g);
            int8_t align = l->align != TUI_ALIGN_NONE ? l->align : p->alignment;
            lvec wrapped = {0};
            wrap_line(a, g, n, ta.width, p->wrap == TUI_WRAP_TRIM, &wrapped);
            for (size_t k = 0; k < wrapped.n; k++) {
                if (skip) {
                    skip--;
                    continue;
                }
                uint16_t w = 0;
                for (size_t j = 0; j < wrapped.lines[k].n; j++) w = (uint16_t)(w + gw(&wrapped.lines[k].g[j]));
                render_line(wrapped.lines[k].g, wrapped.lines[k].n, w, align, ta, buf, y);
                y++;
                if (y >= ta.height) return;
            }
        }
        return;
    }
    for (size_t li = p->scroll_y; li < p->text.n; li++) {
        const tui_line *l = &p->text.lines[li];
        tui_sgrapheme *g;
        size_t n = tui_line_graphemes(a, l, p->text.style, &g);
        int8_t align = l->align != TUI_ALIGN_NONE ? l->align : p->alignment;
        gvec cur = {0};
        uint16_t width = 0, hoff = p->scroll_x;
        for (size_t i = 0; i < n; i++) {
            uint16_t w = gw(&g[i]);
            if (w > ta.width) continue;
            if (width + w > ta.width) break;
            tui_sgrapheme s = g[i];
            if (hoff != 0 && align == TUI_ALIGN_LEFT) {
                if (w > hoff) {
                    /* trim_offset: drop leading graphemes of this symbol worth hoff columns. */
                    size_t start = 0;
                    uint16_t off = hoff;
                    for (size_t k = 0; k < s.len;) {
                        size_t e = tui_grapheme_next(s.s, s.len, k);
                        uint16_t cw = tui_symbol_width(s.s + k, e - k);
                        if (cw <= off) {
                            off = (uint16_t)(off - cw);
                            start = e;
                            k = e;
                        } else break;
                    }
                    s.s += start;
                    s.len -= start;
                    hoff = 0;
                } else {
                    hoff = (uint16_t)(hoff - w);
                    s.len = 0;
                }
            }
            width = (uint16_t)(width + tui_symbol_width(s.s, s.len));
            gpush(a, &cur, s);
        }
        render_line(cur.g, cur.n, width, align, ta, buf, y);
        y++;
        if (y >= ta.height) return;
    }
}
