/* ratatui-core 0.1.2's BufferDiff and Terminal::draw, ratatui-crossterm 0.1.2's CrosstermBackend::draw. */
#include "tui/render.h"

#include "tui/term.h"
#include "tui/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tui_out_put(tui_out *o, const char *s, size_t n) {
    if (o->len + n + 1 > o->cap) {
        size_t cap = o->cap ? o->cap : 4096;
        while (cap < o->len + n + 1) cap *= 2;
        o->data = realloc(o->data, cap);
        if (!o->data) abort();
        o->cap = cap;
    }
    memcpy(o->data + o->len, s, n);
    o->len += n;
    o->data[o->len] = 0;
}

static void outs(tui_out *o, const char *s) { tui_out_put(o, s, strlen(s)); }

void tui_out_free(tui_out *o) {
    free(o->data);
    *o = (tui_out){0};
}

static bool cell_eq(const tui_cell *a, const tui_cell *b) {
    return strcmp(tui_cell_symbol(a), tui_cell_symbol(b)) == 0 && tui_color_eq(a->fg, b->fg) &&
           tui_color_eq(a->bg, b->bg) && a->mod == b->mod;
}

static uint16_t cw(const tui_cell *c) {
    const char *s = tui_cell_symbol(c);
    return tui_symbol_width(s, strlen(s));
}

static void color(tui_out *o, tui_color c, bool fg) {
    char s[32];
    switch (c.kind) {
    case TUI_COLOR_RESET: outs(o, fg ? "\x1b[39m" : "\x1b[49m"); return;
    case TUI_COLOR_RGB: snprintf(s, sizeof s, "\x1b[%d;2;%u;%u;%um", fg ? 38 : 48, c.r, c.g, c.b); break;
    case TUI_COLOR_INDEXED: snprintf(s, sizeof s, "\x1b[%d;5;%um", fg ? 38 : 48, c.r); break;
    default: {
        int base = c.r < 8 ? 30 + c.r : 90 + (c.r - 8);
        snprintf(s, sizeof s, "\x1b[%dm", fg ? base : base + 10);
        break;
    }
    }
    outs(o, s);
}

static void modifier_diff(tui_out *o, uint16_t from, uint16_t to) {
    uint16_t removed = (uint16_t)(from & ~to), added = (uint16_t)(to & ~from);
    if (removed & TUI_REVERSED) outs(o, "\x1b[27m");
    if (removed & TUI_BOLD) {
        outs(o, "\x1b[22m");
        if (to & TUI_DIM) outs(o, "\x1b[2m");
    }
    if (removed & TUI_ITALIC) outs(o, "\x1b[23m");
    if (removed & TUI_UNDERLINED) outs(o, "\x1b[24m");
    if (removed & TUI_DIM) {
        outs(o, "\x1b[22m");
        if (to & TUI_BOLD) outs(o, "\x1b[1m");
    }
    if (removed & TUI_CROSSED_OUT) outs(o, "\x1b[29m");
    if (removed & (TUI_SLOW_BLINK | TUI_RAPID_BLINK)) outs(o, "\x1b[25m");
    if (removed & TUI_HIDDEN) outs(o, "\x1b[28m");
    if (added & TUI_REVERSED) outs(o, "\x1b[7m");
    if (added & TUI_BOLD) outs(o, "\x1b[1m");
    if (added & TUI_ITALIC) outs(o, "\x1b[3m");
    if (added & TUI_UNDERLINED) outs(o, "\x1b[4m");
    if (added & TUI_DIM) outs(o, "\x1b[2m");
    if (added & TUI_CROSSED_OUT) outs(o, "\x1b[9m");
    if (added & TUI_SLOW_BLINK) outs(o, "\x1b[5m");
    if (added & TUI_RAPID_BLINK) outs(o, "\x1b[6m");
    if (added & TUI_HIDDEN) outs(o, "\x1b[8m");
}

typedef struct {
    tui_out *o;
    bool has_last;
    uint16_t lx, ly;
    uint16_t mod;
    tui_color fg, bg;
} pen;

static void emit(pen *p, uint16_t x, uint16_t y, const tui_cell *c) {
    if (!(p->has_last && p->ly == y && p->lx + 1 == x)) {
        char s[32];
        snprintf(s, sizeof s, "\x1b[%u;%uH", (unsigned)y + 1, (unsigned)x + 1);
        outs(p->o, s);
    }
    p->has_last = true;
    p->lx = x;
    p->ly = y;
    if (c->mod != p->mod) {
        modifier_diff(p->o, p->mod, c->mod);
        p->mod = c->mod;
    }
    if (!tui_color_eq(c->fg, p->fg) || !tui_color_eq(c->bg, p->bg)) {
        color(p->o, c->fg, true);
        color(p->o, c->bg, false);
        p->fg = c->fg;
        p->bg = c->bg;
    }
    outs(p->o, tui_cell_symbol(c));
}

void tui_render_diff(const tui_buffer *prev, const tui_buffer *next, tui_out *out) {
    size_t w = next->area.width;
    size_t len = tui_rect_area(next->area) < tui_rect_area(prev->area) ? tui_rect_area(next->area) : tui_rect_area(prev->area);
    pen p = {out, false, 0, 0, 0, TUI_RESET, TUI_RESET};
    for (size_t i = 0; i < len;) {
        const tui_cell *cur = &next->cells[i], *old = &prev->cells[i];
        size_t width = cw(cur);
        if (cell_eq(cur, old)) {
            i += width > 1 ? width : 1;
            continue;
        }
        size_t prev_w = cw(old);
        const char *sym = tui_cell_symbol(cur);
        bool vs16 = width > 1 && strstr(sym, "\xEF\xB8\x8F") != NULL;
        emit(&p, (uint16_t)(i % w + next->area.x), (uint16_t)(i / w + next->area.y), cur);
        if (vs16) {
            /* The cells under it are written when their symbols changed. */
            size_t end = i + width < len ? i + width : len;
            for (size_t j = i + 1; j < end;) {
                size_t jw = cw(&next->cells[j]);
                if (strcmp(tui_cell_symbol(&prev->cells[j]), tui_cell_symbol(&next->cells[j])) != 0)
                    emit(&p, (uint16_t)(j % w + next->area.x), (uint16_t)(j / w + next->area.y), &next->cells[j]);
                j += jw ? jw : 1;
                if (j > end) end = j < len ? j : len;
            }
            i = end;
        } else if (width > 1) {
            i += width;
        } else if (prev_w > width && (old->bg.kind != TUI_COLOR_RESET ||
                                      (old->mod & (TUI_REVERSED | TUI_UNDERLINED | TUI_SLOW_BLINK | TUI_RAPID_BLINK | TUI_CROSSED_OUT)))) {
            size_t end = i + prev_w;
            for (size_t j = i + 1; j < end && j < len;) {
                size_t jw = cw(&next->cells[j]);
                emit(&p, (uint16_t)(j % w + next->area.x), (uint16_t)(j / w + next->area.y), &next->cells[j]);
                j += jw ? jw : 1;
                if (j > end) end = j;
            }
            i = end;
        } else {
            i++;
        }
    }
    outs(out, "\x1b[39m\x1b[49m\x1b[0m");
}

void tui_terminal_init(tui_terminal *t, uint16_t width, uint16_t height) {
    tui_term_size(&width, &height);
    tui_buffer_init(&t->buffers[0], (tui_rect){0, 0, width, height});
    tui_buffer_init(&t->buffers[1], (tui_rect){0, 0, width, height});
    t->current = 0;
    t->cleared = true;
}

void tui_terminal_free(tui_terminal *t) {
    tui_buffer_free(&t->buffers[0]);
    tui_buffer_free(&t->buffers[1]);
}

tui_buffer *tui_terminal_begin(tui_terminal *t) {
    uint16_t w, h;
    tui_term_resized();
    if (tui_term_size(&w, &h) && (w != t->buffers[0].area.width || h != t->buffers[0].area.height)) {
        tui_terminal_free(t);
        tui_buffer_init(&t->buffers[0], (tui_rect){0, 0, w, h});
        tui_buffer_init(&t->buffers[1], (tui_rect){0, 0, w, h});
        t->cleared = true;
    }
    tui_buffer *cur = &t->buffers[t->current];
    tui_buffer_clear(cur, cur->area);
    return cur;
}

void tui_terminal_end(tui_terminal *t) {
    tui_buffer *cur = &t->buffers[t->current], *prev = &t->buffers[1 - t->current];
    tui_out o = {0};
    if (t->cleared) {
        outs(&o, "\x1b[2J");
        tui_buffer_clear(prev, prev->area);
        t->cleared = false;
    }
    tui_render_diff(prev, cur, &o);
    outs(&o, "\x1b[?25l");
    tui_term_write(o.data, o.len);
    tui_out_free(&o);
    t->current = 1 - t->current;
}
