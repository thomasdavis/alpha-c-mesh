/* @ajax/mesh-strbuf: a growable buffer of bytes (always NUL-terminated, so it is text too) with
 * printf, and printf into a new string. Allocation failure aborts. */
#ifndef MESH_STRBUF_H
#define MESH_STRBUF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *p; /* NUL-terminated when not NULL */
    size_t n, cap;
} msb;

void msb_reserve(msb *b, size_t extra);
void msb_put(msb *b, const void *data, size_t n);
void msb_puts(msb *b, const char *s);
void msb_putc(msb *b, char c);
void msb_printf(msb *b, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
void msb_vprintf(msb *b, const char *fmt, va_list ap);
const char *msb_str(msb *b); /* "" when empty */
char *msb_take(msb *b);      /* the string (the caller frees it); b is empty after */
void msb_clear(msb *b);
void msb_free(msb *b);

/* printf into a new string (the caller frees it). */
char *mfmt(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
char *mvfmt(const char *fmt, va_list ap);
char *mstrdup(const char *s);
char *mstrndup(const char *s, size_t n);
void *mxalloc(size_t n);           /* zeroed; aborts when out of memory */
void *mxrealloc(void *p, size_t n);

#endif
