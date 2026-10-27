/* @ajax/tui-render: ratatui's Terminal over crossterm's backend. Each frame is drawn into a fresh
 * buffer; only the cells that differ from the previous frame are written, with ratatui's diff rules
 * (cells under a wide character are skipped, a shrunk wide cell with a visible background
 * repaints what it covered) and crossterm's escapes (cursor moves only when not contiguous,
 * modifier diffs, truecolor). The size is followed: a resize clears and redraws everything. */
#ifndef TUI_RENDER_H
#define TUI_RENDER_H

#include "tui/buffer.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *data;
    size_t len, cap;
} tui_out;

void tui_out_put(tui_out *o, const char *s, size_t n);
void tui_out_free(tui_out *o);

/* The escapes that turn prev into next on a terminal showing prev (same area). */
void tui_render_diff(const tui_buffer *prev, const tui_buffer *next, tui_out *out);

typedef struct {
    tui_buffer buffers[2];
    int current;
    bool cleared; /* the next frame must repaint everything */
} tui_terminal;

/* Sizes the buffers to the terminal (or width x height when the size can't be read). */
void tui_terminal_init(tui_terminal *t, uint16_t width, uint16_t height);
void tui_terminal_free(tui_terminal *t);
/* Starts a frame: follows the terminal's size, and returns the empty buffer to draw into. */
tui_buffer *tui_terminal_begin(tui_terminal *t);
/* Ends it: writes the changes to stdout, hides the cursor, keeps this frame for the next diff. */
void tui_terminal_end(tui_terminal *t);

#endif
