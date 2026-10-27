/* @ajax/tui-layout: ratatui's Layout. A rectangle is split along one direction into segments, one
 * per constraint, by the same Cassowary problem ratatui builds (the same variables, constraints,
 * strengths and rounding), so every segment lands on the same cell. Results are cached, as
 * ratatui's are. */
#ifndef TUI_LAYOUT_H
#define TUI_LAYOUT_H

#include "tui/rect.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { TUI_C_MIN, TUI_C_MAX, TUI_C_LENGTH, TUI_C_PERCENTAGE, TUI_C_RATIO, TUI_C_FILL };

typedef struct {
    uint8_t kind;
    uint32_t a, b; /* the value; Ratio: a / b */
} tui_constraint;

#define TUI_LENGTH(n) ((tui_constraint){TUI_C_LENGTH, (uint32_t)(n), 0})
#define TUI_MIN(n) ((tui_constraint){TUI_C_MIN, (uint32_t)(n), 0})
#define TUI_MAX(n) ((tui_constraint){TUI_C_MAX, (uint32_t)(n), 0})
#define TUI_PERCENTAGE(n) ((tui_constraint){TUI_C_PERCENTAGE, (uint32_t)(n), 0})
#define TUI_RATIO(n, d) ((tui_constraint){TUI_C_RATIO, (uint32_t)(n), (uint32_t)(d)})
#define TUI_FILL(n) ((tui_constraint){TUI_C_FILL, (uint32_t)(n), 0})

typedef enum {
    TUI_FLEX_LEGACY,
    TUI_FLEX_START, /* ratatui's default */
    TUI_FLEX_END,
    TUI_FLEX_CENTER,
    TUI_FLEX_SPACE_BETWEEN,
    TUI_FLEX_SPACE_AROUND,
    TUI_FLEX_SPACE_EVENLY,
} tui_flex;

typedef struct {
    bool vertical;
    const tui_constraint *constraints;
    size_t n;
    uint16_t margin_h, margin_v;
    tui_flex flex;
    int16_t spacing; /* negative: overlap */
} tui_layout;

/* Writes n segments to out. spacers (optional) receives the n + 1 gaps around them. */
void tui_layout_split(const tui_layout *l, tui_rect area, tui_rect *out, tui_rect *spacers);
/* Shorthands with the defaults (Flex::Start, no spacing, no margin). */
void tui_vsplit(tui_rect area, size_t n, const tui_constraint *c, tui_rect *out);
void tui_hsplit(tui_rect area, size_t n, const tui_constraint *c, tui_rect *out);
/* Forgets every cached split. */
void tui_layout_cache_clear(void);

#endif
