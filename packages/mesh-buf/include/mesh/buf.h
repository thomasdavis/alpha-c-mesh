/* @ajax/mesh-buf: a growable byte buffer for building strings and messages.
 *
 * What Rust's String and Vec<u8> do for meshd's host code: append bytes, text
 * and printf output, keep a NUL after the last byte (so data is always a C
 * string when it holds text), hand the bytes over. Allocation failure aborts:
 * meshd never continues with half a message. */
#ifndef MESH_BUF_H
#define MESH_BUF_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *data; /* NULL until something is added; data[len] == 0 after any append */
    size_t len;
    size_t cap;
} mesh_buf;

#define MESH_BUF_INIT {NULL, 0, 0}

void mesh_buf_reserve(mesh_buf *b, size_t extra);
void mesh_buf_put(mesh_buf *b, const void *p, size_t n);
void mesh_buf_puts(mesh_buf *b, const char *s);
void mesh_buf_putc(mesh_buf *b, char c);
void mesh_buf_printf(mesh_buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void mesh_buf_vprintf(mesh_buf *b, const char *fmt, va_list ap);
/* Little-endian integers (wire formats). */
void mesh_buf_u32le(mesh_buf *b, uint32_t v);
void mesh_buf_u64le(mesh_buf *b, uint64_t v);
/* n zero bytes. */
void mesh_buf_zeros(mesh_buf *b, size_t n);
/* The text so far ("" when empty): never NULL. */
const char *mesh_buf_str(const mesh_buf *b);
/* Takes the bytes (NUL-terminated; free() them); the buffer is empty again. */
char *mesh_buf_take(mesh_buf *b);
void mesh_buf_truncate(mesh_buf *b, size_t len);
void mesh_buf_clear(mesh_buf *b);
void mesh_buf_free(mesh_buf *b);

/* Allocation helpers that abort on failure. */
void *mesh_xmalloc(size_t n);
void *mesh_xcalloc(size_t n, size_t size);
void *mesh_xrealloc(void *p, size_t n);
char *mesh_xstrdup(const char *s);
char *mesh_xstrndup(const char *s, size_t n);
/* A new string from printf. */
char *mesh_buf_fmt(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
