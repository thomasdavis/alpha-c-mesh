/* @ajax/mesh-text: growable strings, printf into them, and the Rust
 * formatting the Rust implementation's messages use ({:?} of strings and
 * floats, trim), so ported error messages are byte-for-byte the same.
 *
 * Allocation never fails visibly: out of memory aborts. */
#ifndef MESH_TEXT_H
#define MESH_TEXT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *data; /* NUL-terminated once anything was written; NULL when empty */
    size_t len, cap;
} mesh_text;

void *mesh_alloc(size_t n);             /* zeroed */
void *mesh_realloc(void *p, size_t n);
char *mesh_strdup(const char *s);
char *mesh_strndup(const char *s, size_t n);
char *mesh_fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *mesh_vfmt(const char *fmt, va_list ap);

void mesh_text_putn(mesh_text *t, const char *s, size_t n);
void mesh_text_puts(mesh_text *t, const char *s);
void mesh_text_putc(mesh_text *t, char c);
void mesh_text_printf(mesh_text *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void mesh_text_vprintf(mesh_text *t, const char *fmt, va_list ap);
const char *mesh_text_str(mesh_text *t); /* "" when empty */
char *mesh_text_take(mesh_text *t);      /* the string (caller frees); t is empty after */
void mesh_text_clear(mesh_text *t);
void mesh_text_free(mesh_text *t);

/* Sets *err (when err is not NULL) to a new formatted message, freeing any
 * old one. Returns false, so `return mesh_errf(err, ...)` reads naturally. */
bool mesh_errf(char **err, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Replaces *err with prefix + *err (e.g. "manifest: " + why). */
void mesh_err_prefix(char **err, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* Rust's `{:?}` of a str: quoted, with \" \\ \n \r \t \0 and \u{..} escapes. */
void mesh_text_debug_str(mesh_text *t, const char *s, size_t n);
char *mesh_debug_str(const char *s); /* NUL-terminated input */
/* Rust's `{:?}` of an f64 (shortest round-trip digits; "2.0", "1e16", "1.5e-7", "NaN", "inf"). */
void mesh_text_debug_f64(mesh_text *t, double v);
/* Rust's `{:?}` of an f32. */
void mesh_text_debug_f32(mesh_text *t, float v);

/* Rust's char::is_whitespace on UTF-8, and str::trim / trim_start / trim_end:
 * the result is a (pointer, length) window into s. */
size_t mesh_utf8_ws_len(const char *s, size_t n); /* bytes of one leading whitespace char, 0 if none */
void mesh_trim(const char **s, size_t *n);
void mesh_trim_start(const char **s, size_t *n);
void mesh_trim_end(const char **s, size_t *n);

/* Rust's u32::from_str (and from_str_radix 16): an optional '+', digits only,
 * no overflow. */
bool mesh_parse_u32(const char *s, size_t n, unsigned radix, uint32_t *out);
bool mesh_parse_u64(const char *s, size_t n, unsigned radix, uint64_t *out);
/* Rust's f64::from_str: [+-]? (inf|infinity|nan, any case | digits[.digits][e[+-]digits] | .digits[...]). */
bool mesh_parse_f64(const char *s, size_t n, double *out);

#endif
