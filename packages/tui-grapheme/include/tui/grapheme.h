/* @ajax/tui-grapheme: user-perceived characters. A terminal cell holds one extended grapheme
 * cluster ("e" + combining accent, a flag, a ZWJ emoji family); this splits UTF-8 text into them
 * with the rules of UAX #29 (GB3-GB13, including GB9c Indic conjuncts), the same boundaries as
 * Rust's unicode-segmentation 1.13.3 `graphemes(true)`. */
#ifndef TUI_GRAPHEME_H
#define TUI_GRAPHEME_H

#include <stddef.h>

/* The end (exclusive) of the grapheme cluster that starts at byte start of s[0..len). */
size_t tui_grapheme_next(const char *s, size_t len, size_t start);

#endif
