/* Spans, lines and text: building, measuring, rendering. */
#include "tui/text.h"

#include "tui/grapheme.h"
#include "tui/unicode_width.h"

#include <string.h>

void tui_line_push(tui_arena *a, tui_line *l, tui_span s) {
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 4;
        l->spans = tui_arena_grow(a, l->spans, l->cap * sizeof *l->spans, cap * sizeof *l->spans);
        l->cap = cap;
    }
    l->spans[l->n++] = s;
}

tui_line tui_line_of(tui_arena *a, size_t n, const tui_span *spans) {
    tui_line l = tui_line_empty();
    for (size_t i = 0; i < n; i++) tui_line_push(a, &l, spans[i]);
    return l;
}

void tui_str_lines(const char *s, void (*f)(void *ctx, const char *line, size_t len), void *ctx) {
    size_t len = strlen(s), start = 0;
    while (start < len) {
        const char *nl = memchr(s + start, '\n', len - start);
        size_t end = nl ? (size_t)(nl - s) : len;
        size_t e = end;
        if (nl && e > start && s[e - 1] == '\r') e--;
        f(ctx, s + start, e - start);
        start = nl ? end + 1 : len;
    }
}

typedef struct {
    tui_arena *a;
    tui_line *line;
    tui_text *text;
} lines_ctx;

static void add_span(void *ctx, const char *s, size_t len) {
    lines_ctx *c = ctx;
    tui_line_push(c->a, c->line, tui_span_raw(tui_arena_strndup(c->a, s, len)));
}

tui_line tui_line_from_str(tui_arena *a, const char *s) {
    tui_line l = tui_line_empty();
    lines_ctx c = {a, &l, NULL};
    tui_str_lines(s, add_span, &c);
    return l;
}

void tui_text_push(tui_arena *a, tui_text *t, tui_line l) {
    if (t->n == t->cap) {
        size_t cap = t->cap ? t->cap * 2 : 8;
        t->lines = tui_arena_grow(a, t->lines, t->cap * sizeof *t->lines, cap * sizeof *t->lines);
        t->cap = cap;
    }
    t->lines[t->n++] = l;
}

static void add_line(void *ctx, const char *s, size_t len) {
    lines_ctx *c = ctx;
    tui_line l = tui_line_empty();
    tui_line_push(c->a, &l, tui_span_raw(tui_arena_strndup(c->a, s, len)));
    tui_text_push(c->a, c->text, l);
}

tui_text tui_text_from_str(tui_arena *a, const char *s) {
    tui_text t = tui_text_empty();
    lines_ctx c = {a, NULL, &t};
    tui_str_lines(s, add_line, &c);
    return t;
}

tui_text tui_text_of(tui_arena *a, size_t n, const tui_line *lines) {
    tui_text t = tui_text_empty();
    for (size_t i = 0; i < n; i++) tui_text_push(a, &t, lines[i]);
    return t;
}

size_t tui_span_width(const tui_span *s) { return tui_cstr_width(s->s); }

size_t tui_line_width(const tui_line *l) {
    size_t w = 0;
    for (size_t i = 0; i < l->n; i++) w += tui_span_width(&l->spans[i]);
    return w;
}

size_t tui_text_width(const tui_text *t) {
    size_t w = 0;
    for (size_t i = 0; i < t->n; i++) {
        size_t lw = tui_line_width(&t->lines[i]);
        if (lw > w) w = lw;
    }
    return w;
}

size_t tui_line_graphemes(tui_arena *a, const tui_line *l, tui_style base, tui_sgrapheme **out) {
    tui_style ls = tui_style_patch(base, l->style);
    size_t n = 0, cap = 0;
    tui_sgrapheme *g = NULL;
    for (size_t k = 0; k < l->n; k++) {
        const tui_span *sp = &l->spans[k];
        tui_style st = tui_style_patch(ls, sp->style);
        size_t len = strlen(sp->s);
        for (size_t i = 0; i < len;) {
            size_t end = tui_grapheme_next(sp->s, len, i);
            if (!tui_symbol_has_control(sp->s + i, end - i)) {
                if (n == cap) {
                    size_t nc = cap ? cap * 2 : 64;
                    g = tui_arena_grow(a, g, cap * sizeof *g, nc * sizeof *g);
                    cap = nc;
                }
                g[n++] = (tui_sgrapheme){sp->s + i, end - i, st};
            }
            i = end;
        }
    }
    *out = g;
    return n;
}

void tui_span_render(const tui_span *sp, tui_rect area, tui_buffer *b) {
    area = tui_rect_intersection(area, b->area);
    if (tui_rect_empty(area)) return;
    uint16_t x = area.x, y = area.y;
    size_t len = strlen(sp->s), i = 0;
    int idx = 0;
    while (i < len) {
        size_t end = tui_grapheme_next(sp->s, len, i);
        const char *g = sp->s + i;
        size_t glen = end - i;
        i = end;
        if (tui_symbol_has_control(g, glen)) continue;
        uint16_t w = tui_symbol_width(g, glen);
        uint16_t next = tui_sat_add(x, w);
        if (next > tui_rect_right(area)) break;
        tui_style st = sp->style;
        if (idx == 0) {
            tui_cell *c = tui_buffer_cell(b, x, y);
            tui_cell_set_symbol(c, g, glen);
            tui_cell_set_style(c, st);
        } else if (x == area.x) {
            tui_cell *c = tui_buffer_cell(b, x, y);
            tui_cell_append_symbol(c, g, glen);
            tui_cell_set_style(c, st);
        } else if (w == 0) {
            tui_cell *c = tui_buffer_cell(b, (uint16_t)(x - 1), y);
            tui_cell_append_symbol(c, g, glen);
            tui_cell_set_style(c, st);
        } else {
            tui_cell *c = tui_buffer_cell(b, x, y);
            tui_cell_set_symbol(c, g, glen);
            tui_cell_set_style(c, st);
        }
        for (uint16_t h = (uint16_t)(x + 1); h < next; h++) tui_cell_reset(tui_buffer_cell(b, h, y));
        x = next;
        idx++;
    }
}

/* unicode_truncate_start: the longest suffix of s (whole graphemes) at most max_width wide. */
static const char *truncate_start(const char *s, size_t max_width, size_t *width) {
    size_t len = strlen(s);
    /* Grapheme starts, walked from the end. */
    size_t starts[512], n = 0;
    size_t *st = starts;
    size_t cap = 512;
    tui_arena tmp = {0};
    for (size_t i = 0; i < len; i = tui_grapheme_next(s, len, i)) {
        if (n == cap) {
            size_t *bigger = tui_arena_alloc(&tmp, cap * 2 * sizeof *bigger);
            memcpy(bigger, st, n * sizeof *bigger);
            st = bigger;
            cap *= 2;
        }
        st[n++] = i;
    }
    size_t at = len, sum = 0;
    for (size_t k = n; k-- > 0;) {
        size_t end = k + 1 < n ? st[k + 1] : len;
        size_t w = tui_str_width(s + st[k], end - st[k]);
        if (sum + w > max_width) break;
        sum += w;
        at = st[k];
    }
    tui_arena_free(&tmp);
    *width = at == len ? 0 : sum;
    return s + at;
}

static void render_spans(tui_arena *a, const tui_line *l, tui_rect area, tui_buffer *b, size_t skip) {
    for (size_t k = 0; k < l->n; k++) {
        const tui_span *sp = &l->spans[k];
        size_t sw = tui_span_width(sp);
        if (skip >= sw) {
            skip -= sw;
            continue;
        }
        size_t avail = sw - skip;
        skip = 0;
        tui_span shown = *sp;
        size_t shown_w = sw;
        uint16_t offset = 0;
        if (sw > avail) {
            size_t actual;
            shown.s = truncate_start(sp->s, avail, &actual);
            shown_w = actual;
            offset = (uint16_t)(avail - actual);
        }
        area = tui_rect_indent_x(area, offset);
        if (tui_rect_empty(area)) break;
        tui_span_render(&shown, area, b);
        area = tui_rect_indent_x(area, (uint16_t)(shown_w > 0xFFFF ? 0xFFFF : shown_w));
    }
    (void)a;
}

void tui_line_render(tui_arena *a, const tui_line *l, tui_rect area, tui_buffer *b, int8_t parent) {
    area = tui_rect_intersection(area, b->area);
    if (tui_rect_empty(area)) return;
    area.height = 1;
    size_t lw = tui_line_width(l);
    if (lw == 0) return;
    tui_buffer_set_style(b, area, l->style);
    int8_t align = l->align != TUI_ALIGN_NONE ? l->align : parent;
    size_t aw = area.width;
    if (lw <= aw) {
        size_t indent = align == TUI_ALIGN_CENTER ? (aw - lw) / 2 : align == TUI_ALIGN_RIGHT ? aw - lw : 0;
        render_spans(a, l, tui_rect_indent_x(area, (uint16_t)indent), b, 0);
    } else {
        size_t skip = align == TUI_ALIGN_CENTER ? (lw - aw) / 2 : align == TUI_ALIGN_RIGHT ? lw - aw : 0;
        render_spans(a, l, area, b, skip);
    }
}

void tui_text_render(tui_arena *a, const tui_text *t, tui_rect area, tui_buffer *b) {
    area = tui_rect_intersection(area, b->area);
    tui_buffer_set_style(b, area, t->style);
    for (size_t i = 0; i < t->n && i < area.height; i++)
        tui_line_render(a, &t->lines[i], tui_rect_row(area, (uint16_t)i), b, t->align);
}

uint16_t tui_buffer_set_line(tui_buffer *b, uint16_t x, uint16_t y, const tui_line *l, uint16_t max_width) {
    uint16_t remaining = max_width;
    for (size_t k = 0; k < l->n; k++) {
        if (remaining == 0) break;
        const tui_span *sp = &l->spans[k];
        uint16_t nx = tui_buffer_set_stringn(b, x, y, sp->s, strlen(sp->s), remaining, tui_style_patch(l->style, sp->style));
        uint16_t w = tui_sat_sub(nx, x);
        x = nx;
        remaining = tui_sat_sub(remaining, w);
    }
    return x;
}
