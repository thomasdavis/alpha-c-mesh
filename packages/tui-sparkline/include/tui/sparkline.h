/* @ajax/tui-sparkline: ratatui's Sparkline: one column per value, bars in eighths of a cell
 * scaled to max (or the largest value), drawn bottom up. */
#ifndef TUI_SPARKLINE_H
#define TUI_SPARKLINE_H

#include "tui/block.h"

typedef struct {
    const uint64_t *data;
    size_t n;
    bool has_max;
    uint64_t max;
    tui_style style;
    const tui_block *block;
    bool right_to_left;
} tui_sparkline;

void tui_sparkline_render(tui_arena *a, const tui_sparkline *s, tui_rect area, tui_buffer *buf);

#endif
