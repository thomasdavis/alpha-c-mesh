/* @ajax/tui-style: ratatui's Color, Modifier and Style. A style says only what it changes: an
 * unset colour leaves the cell's, add/sub modifiers turn bits on and off, and patching one style
 * onto another keeps the latter's choices (Style::patch). Header-only. */
#ifndef TUI_STYLE_H
#define TUI_STYLE_H

#include <stdbool.h>
#include <stdint.h>

enum { TUI_COLOR_RESET, TUI_COLOR_RGB, TUI_COLOR_INDEXED, TUI_COLOR_NAMED };

/* kind TUI_COLOR_RGB: r g b; TUI_COLOR_INDEXED: r is the 256-colour index; TUI_COLOR_NAMED: r is
 * ratatui's order (Black 0 .. White 15). */
typedef struct {
    uint8_t kind, r, g, b;
} tui_color;

#define TUI_RGB(R, G, B) ((tui_color){TUI_COLOR_RGB, (R), (G), (B)})
#define TUI_RESET ((tui_color){TUI_COLOR_RESET, 0, 0, 0})
#define TUI_NAMED(i) ((tui_color){TUI_COLOR_NAMED, (i), 0, 0})
#define TUI_BLACK TUI_NAMED(0)
#define TUI_RED TUI_NAMED(1)
#define TUI_GREEN TUI_NAMED(2)
#define TUI_YELLOW TUI_NAMED(3)
#define TUI_BLUE TUI_NAMED(4)
#define TUI_MAGENTA TUI_NAMED(5)
#define TUI_CYAN TUI_NAMED(6)
#define TUI_GRAY TUI_NAMED(7)
#define TUI_DARK_GRAY TUI_NAMED(8)
#define TUI_WHITE TUI_NAMED(15)

enum {
    TUI_BOLD = 1 << 0,
    TUI_DIM = 1 << 1,
    TUI_ITALIC = 1 << 2,
    TUI_UNDERLINED = 1 << 3,
    TUI_SLOW_BLINK = 1 << 4,
    TUI_RAPID_BLINK = 1 << 5,
    TUI_REVERSED = 1 << 6,
    TUI_HIDDEN = 1 << 7,
    TUI_CROSSED_OUT = 1 << 8,
};

typedef struct {
    tui_color fg, bg;
    bool has_fg, has_bg;
    uint16_t add, sub; /* modifiers turned on, turned off */
} tui_style;

static inline bool tui_color_eq(tui_color a, tui_color b) {
    return a.kind == b.kind && a.r == b.r && a.g == b.g && a.b == b.b;
}

static inline tui_style tui_style_new(void) { return (tui_style){0}; }
static inline tui_style tui_style_fg(tui_style s, tui_color c) {
    s.fg = c;
    s.has_fg = true;
    return s;
}
static inline tui_style tui_style_bg(tui_style s, tui_color c) {
    s.bg = c;
    s.has_bg = true;
    return s;
}
static inline tui_style tui_style_add(tui_style s, uint16_t m) {
    s.sub = (uint16_t)(s.sub & ~m);
    s.add = (uint16_t)(s.add | m);
    return s;
}
static inline tui_style tui_style_remove(tui_style s, uint16_t m) {
    s.add = (uint16_t)(s.add & ~m);
    s.sub = (uint16_t)(s.sub | m);
    return s;
}
static inline tui_style tui_style_bold(tui_style s) { return tui_style_add(s, TUI_BOLD); }
/* Shorthands: a style with just a foreground (and bold). */
static inline tui_style tui_fg(tui_color c) { return tui_style_fg(tui_style_new(), c); }
static inline tui_style tui_fg_bold(tui_color c) { return tui_style_bold(tui_fg(c)); }
static inline tui_style tui_fg_bg(tui_color fg, tui_color bg) { return tui_style_bg(tui_fg(fg), bg); }

/* Style::patch: other's colours win when set; other's modifiers apply over self's. */
static inline tui_style tui_style_patch(tui_style self, tui_style other) {
    if (other.has_fg) {
        self.fg = other.fg;
        self.has_fg = true;
    }
    if (other.has_bg) {
        self.bg = other.bg;
        self.has_bg = true;
    }
    self.add = (uint16_t)(self.add & ~other.sub);
    self.add = (uint16_t)(self.add | other.add);
    self.sub = (uint16_t)(self.sub & ~other.add);
    self.sub = (uint16_t)(self.sub | other.sub);
    return self;
}

static inline bool tui_style_eq(tui_style a, tui_style b) {
    return a.has_fg == b.has_fg && a.has_bg == b.has_bg && (!a.has_fg || tui_color_eq(a.fg, b.fg)) &&
           (!a.has_bg || tui_color_eq(a.bg, b.bg)) && a.add == b.add && a.sub == b.sub;
}

#endif
