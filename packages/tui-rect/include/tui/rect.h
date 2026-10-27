/* @ajax/tui-rect: ratatui's Rect: a region of cells with u16 coordinates where every edge
 * computation saturates instead of wrapping. Header-only. */
#ifndef TUI_RECT_H
#define TUI_RECT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t x, y, width, height;
} tui_rect;

static inline uint16_t tui_sat_add(uint16_t a, uint16_t b) { return (uint16_t)((uint32_t)a + b > 0xFFFF ? 0xFFFF : a + b); }
static inline uint16_t tui_sat_sub(uint16_t a, uint16_t b) { return (uint16_t)(a > b ? a - b : 0); }
static inline uint16_t tui_min16(uint16_t a, uint16_t b) { return a < b ? a : b; }
static inline uint16_t tui_max16(uint16_t a, uint16_t b) { return a > b ? a : b; }

/* Rect::new: width and height cut so the far edges fit in u16. */
static inline tui_rect tui_rect_new(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    return (tui_rect){x, y, (uint16_t)(tui_sat_add(x, w) - x), (uint16_t)(tui_sat_add(y, h) - y)};
}
static inline uint16_t tui_rect_left(tui_rect r) { return r.x; }
static inline uint16_t tui_rect_top(tui_rect r) { return r.y; }
static inline uint16_t tui_rect_right(tui_rect r) { return tui_sat_add(r.x, r.width); }
static inline uint16_t tui_rect_bottom(tui_rect r) { return tui_sat_add(r.y, r.height); }
static inline uint32_t tui_rect_area(tui_rect r) { return (uint32_t)r.width * r.height; }
static inline bool tui_rect_empty(tui_rect r) { return r.width == 0 || r.height == 0; }
static inline bool tui_rect_contains(tui_rect r, uint16_t x, uint16_t y) {
    return x >= r.x && x < tui_rect_right(r) && y >= r.y && y < tui_rect_bottom(r);
}
/* Rect::inner(Margin): ZERO when the margins don't fit. */
static inline tui_rect tui_rect_inner(tui_rect r, uint16_t horizontal, uint16_t vertical) {
    uint32_t dh = (uint32_t)horizontal * 2, dv = (uint32_t)vertical * 2;
    if (dh > 0xFFFF) dh = 0xFFFF;
    if (dv > 0xFFFF) dv = 0xFFFF;
    if (r.width < dh || r.height < dv) return (tui_rect){0, 0, 0, 0};
    return (tui_rect){tui_sat_add(r.x, horizontal), tui_sat_add(r.y, vertical), (uint16_t)(r.width - dh),
                      (uint16_t)(r.height - dv)};
}
static inline tui_rect tui_rect_intersection(tui_rect a, tui_rect b) {
    uint16_t x1 = tui_max16(a.x, b.x), y1 = tui_max16(a.y, b.y);
    uint16_t x2 = tui_min16(tui_rect_right(a), tui_rect_right(b)), y2 = tui_min16(tui_rect_bottom(a), tui_rect_bottom(b));
    return (tui_rect){x1, y1, tui_sat_sub(x2, x1), tui_sat_sub(y2, y1)};
}
static inline tui_rect tui_rect_union(tui_rect a, tui_rect b) {
    uint16_t x1 = tui_min16(a.x, b.x), y1 = tui_min16(a.y, b.y);
    uint16_t x2 = tui_max16(tui_rect_right(a), tui_rect_right(b)), y2 = tui_max16(tui_rect_bottom(a), tui_rect_bottom(b));
    return (tui_rect){x1, y1, tui_sat_sub(x2, x1), tui_sat_sub(y2, y1)};
}
/* Rect::indent_x: moves the left edge right by offset, keeping the right edge. */
static inline tui_rect tui_rect_indent_x(tui_rect r, uint16_t offset) {
    r.x = tui_sat_add(r.x, offset);
    r.width = tui_sat_sub(r.width, offset);
    return r;
}
/* Row i of r (Rect::rows), height 1. */
static inline tui_rect tui_rect_row(tui_rect r, uint16_t i) { return (tui_rect){r.x, (uint16_t)(r.y + i), r.width, 1}; }
static inline bool tui_rect_eq(tui_rect a, tui_rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

#endif
