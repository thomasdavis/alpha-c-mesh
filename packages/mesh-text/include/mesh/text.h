/* @ajax/mesh-text: growable strings and string lists, the containers the fleet's Rust used
 * (`String`, `Vec<String>`) and its splitting (`split`, `split_whitespace`, `split_once`).
 *
 * Allocation failure aborts (the Rust aborts too): callers never check for NULL. */
#ifndef MESH_TEXT_H
#define MESH_TEXT_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- strings */

void *mt_alloc(size_t n);             /* zeroed */
void *mt_realloc(void *p, size_t n);
char *mt_strdup(const char *s);
char *mt_strndup(const char *s, size_t n);
char *mt_fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *mt_vfmt(const char *fmt, va_list ap);
bool mt_eq(const char *a, const char *b);          /* both NULL, or equal */
bool mt_starts(const char *s, const char *prefix);
bool mt_ends(const char *s, const char *suffix);
bool mt_contains(const char *s, const char *needle);
/* Replaces *dst with src (a copy), freeing what was there. */
void mt_set(char **dst, const char *src);

/* ---- a growable string (always NUL-terminated once anything was added) */

typedef struct {
    char *s;
    size_t len, cap;
} mt_buf;

void mt_putn(mt_buf *b, const char *s, size_t n);
void mt_puts(mt_buf *b, const char *s);
void mt_putc(mt_buf *b, char c);
void mt_printf(mt_buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
const char *mt_str(mt_buf *b); /* "" when empty */
char *mt_take(mt_buf *b);      /* the string (never NULL), b emptied */
void mt_buf_free(mt_buf *b);

/* ---- a list of strings */

typedef struct {
    char **v;
    size_t n, cap;
} mt_strs;

void mt_push(mt_strs *l, const char *s);   /* a copy */
void mt_push_own(mt_strs *l, char *s);     /* takes s */
void mt_strs_free(mt_strs *l);
mt_strs mt_strs_clone(const mt_strs *l);
bool mt_strs_eq(const mt_strs *a, const mt_strs *b);
bool mt_strs_has(const mt_strs *l, const char *s);
long mt_strs_find(const mt_strs *l, const char *s); /* index, or -1 */
void mt_strs_remove(mt_strs *l, size_t i);
char *mt_join(const mt_strs *l, const char *sep);    /* new string; "" for none */
/* Rust's `s.split(c)`: "a,,b" is ["a", "", "b"], "" is [""]. */
mt_strs mt_split(const char *s, char c);
/* Rust's `s.split_whitespace()`: ASCII whitespace runs, no empty items. */
mt_strs mt_words(const char *s);
/* Rust's `s.lines()`: split on "\n" (a trailing "\r" removed), no final empty line. */
mt_strs mt_lines(const char *s);
/* Rust's `s.split_once(c)`: false when c is absent; else *a and *b are new strings. */
bool mt_split_once(const char *s, char c, char **a, char **b);
/* Rust's `s.trim()` (ASCII whitespace): a new string. */
char *mt_trim(const char *s);

#endif
