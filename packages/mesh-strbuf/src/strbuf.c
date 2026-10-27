/* @ajax/mesh-strbuf: growable buffers and formatted strings. */
#include "mesh/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void oom(void) {
    fputs("mesh-strbuf: out of memory\n", stderr);
    abort();
}

void *mxalloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) oom();
    return p;
}

void *mxrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) oom();
    return p;
}

void msb_reserve(msb *b, size_t extra) {
    if (b->n + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < b->n + extra + 1) cap *= 2;
    b->p = mxrealloc(b->p, cap);
    b->cap = cap;
    b->p[b->n] = 0;
}

void msb_put(msb *b, const void *data, size_t n) {
    msb_reserve(b, n);
    if (n) memcpy(b->p + b->n, data, n);
    b->n += n;
    b->p[b->n] = 0;
}

void msb_puts(msb *b, const char *s) { msb_put(b, s, strlen(s)); }
void msb_putc(msb *b, char c) { msb_put(b, &c, 1); }

void msb_vprintf(msb *b, const char *fmt, va_list ap) {
    va_list cp;
    va_copy(cp, ap);
    int n = vsnprintf(NULL, 0, fmt, cp);
    va_end(cp);
    if (n < 0) return;
    msb_reserve(b, (size_t)n);
    vsnprintf(b->p + b->n, (size_t)n + 1, fmt, ap);
    b->n += (size_t)n;
}

void msb_printf(msb *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    msb_vprintf(b, fmt, ap);
    va_end(ap);
}

const char *msb_str(msb *b) { return b->p ? b->p : ""; }

char *msb_take(msb *b) {
    msb_reserve(b, 0);
    char *s = b->p;
    *b = (msb){0};
    return s;
}

void msb_clear(msb *b) {
    b->n = 0;
    if (b->p) b->p[0] = 0;
}

void msb_free(msb *b) {
    free(b->p);
    *b = (msb){0};
}

char *mvfmt(const char *fmt, va_list ap) {
    msb b = {0};
    msb_vprintf(&b, fmt, ap);
    return msb_take(&b);
}

char *mfmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = mvfmt(fmt, ap);
    va_end(ap);
    return s;
}

char *mstrndup(const char *s, size_t n) {
    char *d = mxalloc(n + 1);
    memcpy(d, s, n);
    return d;
}

char *mstrdup(const char *s) { return mstrndup(s, strlen(s)); }
