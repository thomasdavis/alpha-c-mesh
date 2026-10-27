/* @ajax/tui-paragraph: ratatui's Paragraph and its line composers (reflow.rs): WordWrapper, which
 * breaks at whitespace and, with trim, drops leading whitespace of wrapped lines; and
 * LineTruncator, which cuts each line at the width. */
#ifndef TUI_PARAGRAPH_H
#define TUI_PARAGRAPH_H

#include "tui/block.h"

enum { TUI_WRAP_NONE, TUI_WRAP_TRIM, TUI_WRAP_KEEP };

typedef struct {
    tui_text text;
    const tui_block *block; /* optional */
    tui_style style;
    int wrap;                /* TUI_WRAP_* */
    uint16_t scroll_y, scroll_x;
    int8_t alignment;        /* TUI_ALIGN_LEFT by default (a line's own wins) */
} tui_paragraph;

static inline tui_paragraph tui_paragraph_new(tui_text t) {
    tui_paragraph p = {0};
    p.text = t;
    p.alignment = TUI_ALIGN_LEFT;
    return p;
}

void tui_paragraph_render(tui_arena *a, const tui_paragraph *p, tui_rect area, tui_buffer *buf);

#endif
