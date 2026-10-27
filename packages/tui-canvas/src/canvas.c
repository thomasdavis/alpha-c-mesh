/* ratatui-widgets 0.3.2's canvas.rs (PatternGrid 2x4 for braille, CharGrid, HalfBlockGrid, Painter,
 * Context, Canvas) and canvas/line.rs, points.rs, with line-clipping 0.3's Cohen-Sutherland. */
#include "tui/canvas.h"

#include "tui/utf8.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    bool has_symbol, has_fg, has_bg;
    uint32_t symbol;
    tui_color fg, bg;
} layer_cell;

typedef struct layer {
    layer_cell *cells;
    struct layer *next;
} layer;

typedef struct {
    tui_marker marker;
    uint16_t width, height;
    uint8_t *pattern;      /* braille: one byte per cell */
    bool *has_color;       /* braille and char grids: per cell */
    tui_color *color;      /* per cell (half blocks: per pixel, 2 rows a cell) */
    bool *has_pixel;       /* half blocks: per pixel */
} grid;

struct tui_canvas {
    tui_arena *a;
    uint16_t width, height;
    double xb[2], yb[2];
    grid g;
    bool dirty;
    layer *first, *last;
    struct label {
        double x, y;
        tui_line line;
    } *labels;
    size_t nlabels, caplabels;
};

static void grid_init(tui_arena *a, grid *g, uint16_t w, uint16_t h, tui_marker m) {
    size_t n = (size_t)w * h;
    *g = (grid){.marker = m, .width = w, .height = h};
    if (m == TUI_MARKER_HALF_BLOCK) {
        g->color = tui_arena_alloc(a, (n ? 2 * n : 1) * sizeof *g->color);
        g->has_pixel = tui_arena_alloc(a, n ? 2 * n : 1);
    } else {
        g->color = tui_arena_alloc(a, (n ? n : 1) * sizeof *g->color);
        g->has_color = tui_arena_alloc(a, n ? n : 1);
        if (m == TUI_MARKER_BRAILLE) g->pattern = tui_arena_alloc(a, n ? n : 1);
    }
}

static void grid_resolution(const grid *g, double *rx, double *ry) {
    if (g->marker == TUI_MARKER_BRAILLE) {
        *rx = (double)g->width * 2.0;
        *ry = (double)g->height * 4.0;
    } else if (g->marker == TUI_MARKER_HALF_BLOCK) {
        *rx = (double)g->width;
        *ry = (double)g->height * 2.0;
    } else {
        *rx = (double)g->width;
        *ry = (double)g->height;
    }
}

static void grid_paint(grid *g, size_t x, size_t y, tui_color c) {
    size_t n = (size_t)g->width * g->height;
    if (g->marker == TUI_MARKER_BRAILLE) {
        size_t i = (y / 4) * g->width + x / 2; /* x is not checked against the width, as ratatui's */
        if (i < n) {
            g->pattern[i] |= (uint8_t)(1u << ((x % 2) + 2 * (y % 4)));
            g->has_color[i] = true;
            g->color[i] = c;
        }
    } else if (g->marker == TUI_MARKER_HALF_BLOCK) {
        if (y < 2u * g->height && x < g->width) {
            g->has_pixel[y * g->width + x] = true;
            g->color[y * g->width + x] = c;
        }
    } else {
        size_t i = y * g->width + x;
        if (i < n) {
            g->has_color[i] = true;
            g->color[i] = c;
        }
    }
}

/* The braille character for a row-major dot pattern (bit (x % 2) + 2 (y % 4)). */
static uint32_t braille(uint8_t p) {
    static const uint8_t dot[8] = {0x01, 0x08, 0x02, 0x10, 0x04, 0x20, 0x40, 0x80};
    uint32_t cp = 0x2800;
    for (int b = 0; b < 8; b++)
        if (p & (1u << b)) cp |= dot[b];
    return cp;
}

static layer *grid_save(tui_arena *a, const grid *g) {
    size_t n = (size_t)g->width * g->height;
    layer *l = tui_arena_alloc(a, sizeof *l);
    l->cells = tui_arena_alloc(a, (n ? n : 1) * sizeof *l->cells);
    for (size_t i = 0; i < n; i++) {
        layer_cell *c = &l->cells[i];
        switch (g->marker) {
        case TUI_MARKER_BRAILLE:
            if (g->pattern[i]) {
                c->has_symbol = true;
                c->symbol = braille(g->pattern[i]);
            }
            if (g->has_color[i]) {
                c->has_fg = true;
                c->fg = g->color[i];
            }
            break;
        case TUI_MARKER_HALF_BLOCK: {
            size_t x = i % g->width, row = i / g->width;
            size_t up = 2 * row * g->width + x, lo = (2 * row + 1) * g->width + x;
            bool hu = g->has_pixel[up], hl = g->has_pixel[lo];
            tui_color u = g->color[up], d = g->color[lo];
            if (!hu && hl) *c = (layer_cell){true, true, false, 0x2584, d, {0}};
            else if (hu && !hl) *c = (layer_cell){true, true, false, 0x2580, u, {0}};
            else if (hu && hl && tui_color_eq(u, d)) *c = (layer_cell){true, true, true, 0x2588, u, d};
            else if (hu && hl) *c = (layer_cell){true, true, true, 0x2580, u, d};
            break;
        }
        default:
            if (g->has_color[i]) {
                c->has_symbol = true;
                c->symbol = g->marker == TUI_MARKER_BLOCK ? 0x2588 : g->marker == TUI_MARKER_BAR ? 0x2584 : 0x2022;
                c->has_fg = true;
                c->fg = g->color[i];
                if (g->marker == TUI_MARKER_BLOCK) {
                    c->has_bg = true;
                    c->bg = g->color[i];
                }
            }
            break;
        }
    }
    return l;
}

tui_canvas *tui_canvas_new(tui_arena *a, uint16_t width, uint16_t height, const double x_bounds[2],
                           const double y_bounds[2], tui_marker marker) {
    tui_canvas *c = tui_arena_alloc(a, sizeof *c);
    c->a = a;
    c->width = width;
    c->height = height;
    memcpy(c->xb, x_bounds, sizeof c->xb);
    memcpy(c->yb, y_bounds, sizeof c->yb);
    grid_init(a, &c->g, width, height, marker);
    return c;
}

static void finish(tui_canvas *c) {
    if (!c->dirty) return;
    layer *l = grid_save(c->a, &c->g);
    if (c->last) c->last->next = l;
    else c->first = l;
    c->last = l;
    grid_init(c->a, &c->g, c->width, c->height, c->g.marker);
    c->dirty = false;
}

void tui_canvas_marker(tui_canvas *c, tui_marker m) {
    finish(c);
    grid_init(c->a, &c->g, c->width, c->height, m);
}

/* Painter::get_point. */
static bool get_point(const tui_canvas *c, double x, double y, size_t *px, size_t *py) {
    double left = c->xb[0], right = c->xb[1], bottom = c->yb[0], top = c->yb[1];
    if (x < left || x > right || y < bottom || y > top) return false;
    double width = right - left, height = top - bottom;
    if (width <= 0.0 || height <= 0.0) return false;
    double rx, ry;
    grid_resolution(&c->g, &rx, &ry);
    double fx = round((x - left) * (rx - 1.0) / width), fy = round((top - y) * (ry - 1.0) / height);
    *px = fx <= 0 || fx != fx ? 0 : (size_t)fx;
    *py = fy <= 0 || fy != fy ? 0 : (size_t)fy;
    return true;
}

void tui_canvas_points(tui_canvas *c, const tui_point *pts, size_t n, tui_color color) {
    c->dirty = true;
    for (size_t i = 0; i < n; i++) {
        size_t x, y;
        if (get_point(c, pts[i].x, pts[i].y, &x, &y)) grid_paint(&c->g, x, y, color);
    }
}

/* line-clipping 0.3's Cohen-Sutherland. */
enum { R_LEFT = 1, R_RIGHT = 2, R_BOTTOM = 4, R_TOP = 8 };

static int region(double x, double y, const double xb[2], const double yb[2]) {
    int r = 0;
    if (x < xb[0]) r |= R_LEFT;
    else if (x > xb[1]) r |= R_RIGHT;
    if (y < yb[0]) r |= R_BOTTOM;
    else if (y > yb[1]) r |= R_TOP;
    return r;
}

static void intersect(double *x1, double *y1, double x2, double y2, int r, const double xb[2], const double yb[2]) {
    double dx = x2 - *x1, dy = y2 - *y1;
    if (r & R_LEFT) {
        *y1 = *y1 + (xb[0] - *x1) * dy / dx;
        *x1 = xb[0];
    } else if (r & R_RIGHT) {
        *y1 = *y1 + (xb[1] - *x1) * dy / dx;
        *x1 = xb[1];
    } else if (r & R_BOTTOM) {
        *x1 = *x1 + (yb[0] - *y1) * dx / dy;
        *y1 = yb[0];
    } else {
        *x1 = *x1 + (yb[1] - *y1) * dx / dy;
        *y1 = yb[1];
    }
}

static bool clip(const double xb[2], const double yb[2], double *x1, double *y1, double *x2, double *y2) {
    int r1 = region(*x1, *y1, xb, yb), r2 = region(*x2, *y2, xb, yb);
    for (;;) {
        if (r1 & r2) return false;
        if (r1) {
            intersect(x1, y1, *x2, *y2, r1, xb, yb);
            r1 = region(*x1, *y1, xb, yb);
        } else if (r2) {
            intersect(x2, y2, *x1, *y1, r2, xb, yb);
            r2 = region(*x2, *y2, xb, yb);
        } else {
            return true;
        }
    }
}

typedef void (*plot_fn)(tui_canvas *c, size_t x, size_t y, void *arg);

static void line_low(tui_canvas *c, size_t x1, size_t y1, size_t x2, size_t y2, plot_fn f, void *arg) {
    long dx = (long)(x2 - x1), dy = labs((long)y2 - (long)y1);
    long d = 2 * dy - dx;
    size_t y = y1;
    for (size_t x = x1; x <= x2; x++) {
        f(c, x, y, arg);
        if (d > 0) {
            y = y1 > y2 ? (y ? y - 1 : 0) : y + 1;
            d -= 2 * dx;
        }
        d += 2 * dy;
    }
}

static void line_high(tui_canvas *c, size_t x1, size_t y1, size_t x2, size_t y2, plot_fn f, void *arg) {
    long dx = labs((long)x2 - (long)x1), dy = (long)(y2 - y1);
    long d = 2 * dx - dy;
    size_t x = x1;
    for (size_t y = y1; y <= y2; y++) {
        f(c, x, y, arg);
        if (d > 0) {
            x = x1 > x2 ? (x ? x - 1 : 0) : x + 1;
            d -= 2 * dy;
        }
        d += 2 * dx;
    }
}

static void each_line_point(tui_canvas *c, size_t x1, size_t y1, size_t x2, size_t y2, plot_fn f, void *arg) {
    size_t dx = x1 > x2 ? x1 - x2 : x2 - x1, dy = y1 > y2 ? y1 - y2 : y2 - y1;
    if (dx == 0) {
        for (size_t y = y1 < y2 ? y1 : y2; y <= (y1 > y2 ? y1 : y2); y++) f(c, x1, y, arg);
    } else if (dy == 0) {
        for (size_t x = x1 < x2 ? x1 : x2; x <= (x1 > x2 ? x1 : x2); x++) f(c, x, y1, arg);
    } else if (dy < dx) {
        if (x1 > x2) line_low(c, x2, y2, x1, y1, f, arg);
        else line_low(c, x1, y1, x2, y2, f, arg);
    } else if (y1 > y2) {
        line_high(c, x2, y2, x1, y1, f, arg);
    } else {
        line_high(c, x1, y1, x2, y2, f, arg);
    }
}

static void plot(tui_canvas *c, size_t x, size_t y, void *arg) { grid_paint(&c->g, x, y, *(tui_color *)arg); }

void tui_canvas_line(tui_canvas *c, double x1, double y1, double x2, double y2, tui_color color) {
    c->dirty = true;
    if (!clip(c->xb, c->yb, &x1, &y1, &x2, &y2)) return;
    size_t px1, py1, px2, py2;
    if (!get_point(c, x1, y1, &px1, &py1) || !get_point(c, x2, y2, &px2, &py2)) return;
    each_line_point(c, px1, py1, px2, py2, plot, &color);
}

typedef struct {
    size_t fill;
    tui_color color;
} fill_arg;

static void plot_fill(tui_canvas *c, size_t x, size_t y, void *arg) {
    fill_arg *f = arg;
    size_t s = y < f->fill ? y : f->fill, e = y > f->fill ? y : f->fill;
    for (size_t k = s; k <= e; k++) grid_paint(&c->g, x, k, f->color);
}

void tui_canvas_filled_line(tui_canvas *c, double x1, double y1, double x2, double y2, double fill_to_y, tui_color color) {
    c->dirty = true;
    double wx1 = x1, wy1 = y1, wx2 = x2, wy2 = y2;
    if (!clip(c->xb, c->yb, &wx1, &wy1, &wx2, &wy2)) return;
    size_t px1, py1, px2, py2, fx, fy;
    if (!get_point(c, wx1, wy1, &px1, &py1) || !get_point(c, wx2, wy2, &px2, &py2)) return;
    double yf = fill_to_y < c->yb[0] ? c->yb[0] : fill_to_y > c->yb[1] ? c->yb[1] : fill_to_y;
    if (!get_point(c, wx1, yf, &fx, &fy)) return;
    fill_arg f = {fy, color};
    each_line_point(c, px1, py1, px2, py2, plot_fill, &f);
}

void tui_canvas_bar(tui_canvas *c, double x, double y, tui_color color) { tui_canvas_line(c, x, 0.0, x, y, color); }

void tui_canvas_print(tui_canvas *c, double x, double y, tui_line line) {
    if (c->nlabels == c->caplabels) {
        size_t cap = c->caplabels ? c->caplabels * 2 : 4;
        c->labels = tui_arena_grow(c->a, c->labels, c->caplabels * sizeof *c->labels, cap * sizeof *c->labels);
        c->caplabels = cap;
    }
    c->labels[c->nlabels++] = (struct label){x, y, line};
}

void tui_canvas_render(tui_arena *a, tui_canvas *c, const tui_block *block, tui_color background, tui_rect area,
                       tui_buffer *buf) {
    if (block) tui_block_render(a, block, area, buf);
    tui_rect ca = tui_block_inner_opt(block, area);
    if (tui_rect_empty(ca)) return;
    tui_buffer_set_style(buf, ca, tui_style_bg(tui_style_new(), background));
    finish(c);
    size_t width = ca.width;
    for (layer *l = c->first; l; l = l->next) {
        size_t n = (size_t)c->width * c->height;
        for (size_t i = 0; i < n; i++) {
            tui_cell *cell = tui_buffer_cell(buf, (uint16_t)(i % width + ca.x), (uint16_t)(i / width + ca.y));
            if (!cell) continue;
            const layer_cell *lc = &l->cells[i];
            if (lc->has_symbol) {
                char s[4];
                size_t k = tui_utf8_encode(lc->symbol, s);
                tui_cell_set_symbol(cell, s, k);
            }
            if (lc->has_fg) cell->fg = lc->fg;
            if (lc->has_bg) cell->bg = lc->bg;
        }
    }
    double left = c->xb[0], right = c->xb[1], top = c->yb[1], bottom = c->yb[0];
    double w = fabs(c->xb[1] - c->xb[0]), h = fabs(c->yb[1] - c->yb[0]);
    double rx = (double)(ca.width - 1), ry = (double)(ca.height - 1);
    for (size_t i = 0; i < c->nlabels; i++) {
        const struct label *lb = &c->labels[i];
        if (!(lb->x >= left && lb->x <= right && lb->y <= top && lb->y >= bottom)) continue;
        uint16_t x = (uint16_t)((lb->x - left) * rx / w) + ca.x;
        uint16_t y = (uint16_t)((top - lb->y) * ry / h) + ca.y;
        tui_buffer_set_line(buf, x, y, &lb->line, (uint16_t)(tui_rect_right(ca) - x));
    }
}

void tui_canvas_widget(tui_arena *a, const tui_block *block, tui_color background, const double x_bounds[2],
                       const double y_bounds[2], tui_marker marker, void (*paint)(tui_canvas *c, void *arg), void *arg,
                       tui_rect area, tui_buffer *buf) {
    tui_rect ca = tui_block_inner_opt(block, area);
    tui_canvas *c = tui_canvas_new(a, ca.width, ca.height, x_bounds, y_bounds, marker);
    if (paint && !tui_rect_empty(ca)) paint(c, arg);
    tui_canvas_render(a, c, block, background, area, buf);
}
