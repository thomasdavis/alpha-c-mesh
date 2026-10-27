/* @ajax/tui-unicode-width: how many terminal columns text takes, exactly as Rust's unicode-width
 * crate 0.2.2 computes it (UAX #11, ambiguous characters narrow, emoji presentation sequences,
 * ZWJ sequences, the ligatures it knows). ratatui measures every span with it, so a port that
 * must lay out the same frame needs the same answers. */
#ifndef TUI_UNICODE_WIDTH_H
#define TUI_UNICODE_WIDTH_H

#include <stddef.h>
#include <stdint.h>

/* One character's width (0, 1 or 2), or -1 for a control character (Rust: None). */
int tui_char_width(uint32_t cp);
/* A string's width (Rust: UnicodeWidthStr::width). Control characters count 1, "\r\n" counts 1.
 * The string is UTF-8; an invalid byte counts as U+FFFD. */
size_t tui_str_width(const char *s, size_t len);
size_t tui_cstr_width(const char *s);

#endif
