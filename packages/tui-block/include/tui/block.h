/* @ajax/tui-block: ratatui's Block: borders (any of the four sides, in a border set), titles on the
 * top or bottom edge aligned left, centre or right, a style for the whole area and one for the
 * borders, and padding. inner() is the area left for the content. Borders replace what is there
 * (ratatui's default MergeStrategy::Replace). */
#ifndef TUI_BLOCK_H
#define TUI_BLOCK_H

#include "tui/text.h"

enum {
    TUI_BORDER_NONE = 0,
    TUI_BORDER_TOP = 1,
    TUI_BORDER_RIGHT = 2,
    TUI_BORDER_BOTTOM = 4,
    TUI_BORDER_LEFT = 8,
    TUI_BORDER_ALL = 15,
};

enum { TUI_TITLE_DEFAULT = -1, TUI_TITLE_TOP = 0, TUI_TITLE_BOTTOM = 1 };

typedef struct {
    const char *top_left, *top_right, *bottom_left, *bottom_right;
    const char *vertical_left, *vertical_right, *horizontal_top, *horizontal_bottom;
} tui_border_set;

extern const tui_border_set TUI_BORDER_PLAIN, TUI_BORDER_ROUNDED, TUI_BORDER_DOUBLE, TUI_BORDER_THICK;

typedef struct {
    tui_line line;
    int8_t position; /* TUI_TITLE_* */
} tui_block_title;

typedef struct {
    uint8_t borders;
    const tui_border_set *set; /* NULL: plain */
    tui_style border_style, style, titles_style;
    tui_block_title *titles;
    size_t ntitles, captitles;
    int8_t titles_alignment; /* TUI_ALIGN_LEFT by default */
    int8_t titles_position;  /* TUI_TITLE_TOP by default */
    uint16_t pad_left, pad_right, pad_top, pad_bottom;
} tui_block;

static inline tui_block tui_block_new(void) {
    tui_block b = {0};
    b.titles_alignment = TUI_ALIGN_LEFT;
    b.titles_position = TUI_TITLE_TOP;
    return b;
}
static inline tui_block tui_block_bordered(void) {
    tui_block b = tui_block_new();
    b.borders = TUI_BORDER_ALL;
    return b;
}
/* Block::title (position: TUI_TITLE_DEFAULT for the block's). */
void tui_block_add_title(tui_arena *a, tui_block *b, tui_line line, int8_t position);
tui_rect tui_block_inner(const tui_block *b, tui_rect area);
void tui_block_render(tui_arena *a, const tui_block *b, tui_rect area, tui_buffer *buf);
/* Option<Block>::inner_if_some. */
static inline tui_rect tui_block_inner_opt(const tui_block *b, tui_rect area) { return b ? tui_block_inner(b, area) : area; }

#endif
