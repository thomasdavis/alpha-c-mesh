/* @ajax/tui-text: ratatui's Span (a string with a style), Line (spans, a style, an alignment) and
 * Text (lines). Everything is built in a tui_arena and lives as long as the frame. Widths are
 * unicode-width's; rendering follows ratatui's Widget impls for &Span, &Line and &Text. */
#ifndef TUI_TEXT_H
#define TUI_TEXT_H

#include "tui/arena.h"
#include "tui/buffer.h"

#include <stddef.h>
#include <stdint.h>

enum { TUI_ALIGN_NONE = -1, TUI_ALIGN_LEFT = 0, TUI_ALIGN_CENTER = 1, TUI_ALIGN_RIGHT = 2 };

typedef struct {
    const char *s; /* NUL-terminated UTF-8 */
    tui_style style;
} tui_span;

typedef struct {
    tui_span *spans;
    size_t n, cap;
    tui_style style;
    int8_t align; /* TUI_ALIGN_* (NONE: the parent's) */
} tui_line;

typedef struct {
    tui_line *lines;
    size_t n, cap;
    tui_style style;
    int8_t align;
} tui_text;

/* A grapheme with the style it renders in (StyledGrapheme). */
typedef struct {
    const char *s;
    size_t len;
    tui_style style;
} tui_sgrapheme;

static inline tui_span tui_span_raw(const char *s) { return (tui_span){.s = s}; }
static inline tui_span tui_span_styled(const char *s, tui_style st) { return (tui_span){s, st}; }
static inline tui_line tui_line_empty(void) { return (tui_line){.align = TUI_ALIGN_NONE}; }
static inline tui_text tui_text_empty(void) { return (tui_text){.align = TUI_ALIGN_NONE}; }

void tui_line_push(tui_arena *a, tui_line *l, tui_span s);
/* Line::from(Vec<Span>). */
tui_line tui_line_of(tui_arena *a, size_t n, const tui_span *spans);
static inline tui_line tui_line_span(tui_arena *a, tui_span s) { return tui_line_of(a, 1, &s); }
/* Line::from(&str) / Line::raw: one raw span per line of s (str::lines). */
tui_line tui_line_from_str(tui_arena *a, const char *s);
void tui_text_push(tui_arena *a, tui_text *t, tui_line l);
/* Text::from(&str): one line per line of s. */
tui_text tui_text_from_str(tui_arena *a, const char *s);
static inline tui_text tui_text_line(tui_arena *a, tui_line l) {
    tui_text t = tui_text_empty();
    tui_text_push(a, &t, l);
    return t;
}
tui_text tui_text_of(tui_arena *a, size_t n, const tui_line *lines);

/* str::lines: splits at '\n', drops one '\r' before it, no empty last line. Calls f per line. */
void tui_str_lines(const char *s, void (*f)(void *ctx, const char *line, size_t len), void *ctx);

size_t tui_span_width(const tui_span *s);
size_t tui_line_width(const tui_line *l);
size_t tui_text_width(const tui_text *t); /* the widest line */

/* The line's graphemes (control ones left out) with base patched by the line's style and each
 * span's (Line::styled_graphemes). *out is arena memory; returns the count. */
size_t tui_line_graphemes(tui_arena *a, const tui_line *l, tui_style base, tui_sgrapheme **out);

void tui_span_render(const tui_span *s, tui_rect area, tui_buffer *b);
/* Line::render_with_alignment (parent: the alignment to use when the line has none). */
void tui_line_render(tui_arena *a, const tui_line *l, tui_rect area, tui_buffer *b, int8_t parent);
void tui_text_render(tui_arena *a, const tui_text *t, tui_rect area, tui_buffer *b);
/* Buffer::set_line: each span through set_stringn with the line's style patched by the span's.
 * Returns the x after the last cell written. */
uint16_t tui_buffer_set_line(tui_buffer *b, uint16_t x, uint16_t y, const tui_line *l, uint16_t max_width);

#endif
