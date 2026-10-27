/* A growable byte buffer; allocation failure aborts. */
#include "mesh/buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Noreturn void oom(void) {
    fputs("out of memory\n", stderr);
    abort();
}

void *mesh_xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *mesh_xcalloc(size_t n, size_t size) {
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *mesh_xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) oom();
    return p;
}

char *mesh_xstrndup(const char *s, size_t n) {
    char *d = mesh_xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char *mesh_xstrdup(const char *s) { return mesh_xstrndup(s, strlen(s)); }

void mesh_buf_reserve(mesh_buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->len + extra + 1) cap *= 2;
    b->data = mesh_xrealloc(b->data, cap);
    b->cap = cap;
    b->data[b->len] = 0;
}

void mesh_buf_put(mesh_buf *b, const void *p, size_t n) {
    mesh_buf_reserve(b, n);
    if (n) memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
}

void mesh_buf_puts(mesh_buf *b, const char *s) { mesh_buf_put(b, s, strlen(s)); }

void mesh_buf_putc(mesh_buf *b, char c) { mesh_buf_put(b, &c, 1); }

void mesh_buf_vprintf(mesh_buf *b, const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return;
    mesh_buf_reserve(b, (size_t)n);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    b->len += (size_t)n;
}

void mesh_buf_printf(mesh_buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    mesh_buf_vprintf(b, fmt, ap);
    va_end(ap);
}

void mesh_buf_u32le(mesh_buf *b, uint32_t v) {
    uint8_t x[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    mesh_buf_put(b, x, 4);
}

void mesh_buf_u64le(mesh_buf *b, uint64_t v) {
    mesh_buf_u32le(b, (uint32_t)v);
    mesh_buf_u32le(b, (uint32_t)(v >> 32));
}

void mesh_buf_zeros(mesh_buf *b, size_t n) {
    mesh_buf_reserve(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
    b->data[b->len] = 0;
}

const char *mesh_buf_str(const mesh_buf *b) { return b->data ? b->data : ""; }

char *mesh_buf_take(mesh_buf *b) {
    mesh_buf_reserve(b, 0);
    char *s = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return s;
}

void mesh_buf_truncate(mesh_buf *b, size_t len) {
    if (len >= b->len) return;
    b->len = len;
    b->data[len] = 0;
}

void mesh_buf_clear(mesh_buf *b) {
    b->len = 0;
    if (b->data) b->data[0] = 0;
}

void mesh_buf_free(mesh_buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

char *mesh_fmt(const char *fmt, ...) {
    mesh_buf b = MESH_BUF_INIT;
    va_list ap;
    va_start(ap, fmt);
    mesh_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    return mesh_buf_take(&b);
}
