/* @ajax/tui-utf8: UTF-8 as Rust handles it. Decoding follows the Unicode "maximal subpart" rule,
 * the one String::from_utf8_lossy uses, so repaired text is byte-for-byte what Rust would show. */
#ifndef TUI_UTF8_H
#define TUI_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUI_UTF8_REPLACEMENT 0xFFFDu

/* Decodes the character at s[*i] and advances *i past it; an ill-formed subpart decodes as
 * U+FFFD and advances past exactly that subpart. *i must be < len. */
uint32_t tui_utf8_next(const char *s, size_t len, size_t *i);
/* Writes cp's UTF-8 (1-4 bytes) to out; returns the length. */
size_t tui_utf8_encode(uint32_t cp, char out[4]);
bool tui_utf8_valid(const char *s, size_t len);
/* A NUL-terminated, valid UTF-8 copy (malloc'd; free it), each ill-formed subpart replaced by
 * U+FFFD. *out_len (if given) receives its length. */
char *tui_utf8_lossy(const void *bytes, size_t len, size_t *out_len);
/* Characters (Unicode scalar values) in valid UTF-8. */
size_t tui_utf8_count(const char *s, size_t len);

#endif
