/* @ajax/tui-buffer: ratatui's Buffer and Cell. Widgets draw into a buffer; the terminal shows it.
 * A cell holds one grapheme cluster (or nothing: shown as a space), a foreground, a background
 * and modifiers. A wide grapheme occupies its cell and resets the ones it covers. */
#ifndef TUI_BUFFER_H
#define TUI_BUFFER_H

#include "tui/rect.h"
#include "tui/style.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUI_SYM_MAX 32 /* a grapheme longer than this is cut (never in practice) */

typedef struct {
    char sym[TUI_SYM_MAX]; /* NUL-terminated; ignored while none */
    bool none;             /* no symbol yet: reads as " " */
    tui_color fg, bg;
    uint16_t mod;
} tui_cell;

typedef struct {
    tui_rect area;
    tui_cell *cells; /* area.width * area.height, row-major */
} tui_buffer;

extern const tui_cell TUI_CELL_EMPTY;

const char *tui_cell_symbol(const tui_cell *c);
void tui_cell_set_symbol(tui_cell *c, const char *s, size_t len);
void tui_cell_set_str(tui_cell *c, const char *s); /* NUL-terminated */
void tui_cell_append_symbol(tui_cell *c, const char *s, size_t len);
void tui_cell_set_style(tui_cell *c, tui_style st);
void tui_cell_reset(tui_cell *c);
/* The cell's whole look as a style (Cell::style). */
tui_style tui_cell_style(const tui_cell *c);

/* The columns a grapheme takes (ratatui's CellWidth for str): 1 for any one-byte grapheme, else its
 * unicode width plus one per halfwidth (semi-)voiced sound mark. */
uint16_t tui_symbol_width(const char *s, size_t len);
/* Whether a grapheme holds a control character (char::is_control: Cc). */
bool tui_symbol_has_control(const char *s, size_t len);

void tui_buffer_init(tui_buffer *b, tui_rect area); /* every cell empty */
void tui_buffer_free(tui_buffer *b);
/* The cell at (x, y), or NULL outside the buffer. */
tui_cell *tui_buffer_cell(tui_buffer *b, uint16_t x, uint16_t y);
const tui_cell *tui_buffer_cell_c(const tui_buffer *b, uint16_t x, uint16_t y);
/* Buffer::set_stringn: writes s's graphemes from (x, y) while they fit in max_width and the
 * buffer; control graphemes and zero-width ones are skipped. Returns the x after the last. */
uint16_t tui_buffer_set_stringn(tui_buffer *b, uint16_t x, uint16_t y, const char *s, size_t len, size_t max_width,
                                tui_style st);
uint16_t tui_buffer_set_string(tui_buffer *b, uint16_t x, uint16_t y, const char *s, tui_style st);
/* Buffer::set_style: patches every cell of area (cut to the buffer). */
void tui_buffer_set_style(tui_buffer *b, tui_rect area, tui_style st);
/* The Clear widget: resets every cell of area. */
void tui_buffer_clear(tui_buffer *b, tui_rect area);

#endif
