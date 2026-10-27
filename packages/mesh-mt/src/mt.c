/* Strings, buffers and string lists (see the header). */
#include "mesh/mt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Noreturn void oom(void) {
    fputs("out of memory\n", stderr);
    abort();
}

void *mt_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) oom();
    return p;
}

void *mt_realloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) oom();
    return p;
}

char *mt_strndup(const char *s, size_t n) {
    char *d = mt_alloc(n + 1);
    memcpy(d, s, n);
    return d;
}

char *mt_strdup(const char *s) { return mt_strndup(s, strlen(s)); }

char *mt_vfmt(const char *fmt, va_list ap) {
    va_list c;
    va_copy(c, ap);
    int n = vsnprintf(NULL, 0, fmt, c);
    va_end(c);
    if (n < 0) return mt_strdup("");
    char *s = mt_alloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    return s;
}

char *mt_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = mt_vfmt(fmt, ap);
    va_end(ap);
    return s;
}

bool mt_eq(const char *a, const char *b) { return a == b || (a && b && strcmp(a, b) == 0); }
bool mt_starts(const char *s, const char *p) { return strncmp(s, p, strlen(p)) == 0; }
bool mt_ends(const char *s, const char *x) {
    size_t a = strlen(s), b = strlen(x);
    return a >= b && memcmp(s + a - b, x, b) == 0;
}
bool mt_contains(const char *s, const char *n) { return strstr(s, n) != NULL; }

void mt_set(char **dst, const char *src) {
    char *n = src ? mt_strdup(src) : NULL;
    free(*dst);
    *dst = n;
}

void mt_putn(mt_buf *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t c = b->cap ? b->cap : 64;
        while (c < b->len + n + 1) c *= 2;
        b->s = mt_realloc(b->s, c);
        b->cap = c;
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

void mt_puts(mt_buf *b, const char *s) { mt_putn(b, s, strlen(s)); }
void mt_putc(mt_buf *b, char c) { mt_putn(b, &c, 1); }

void mt_printf(mt_buf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = mt_vfmt(fmt, ap);
    va_end(ap);
    mt_puts(b, s);
    free(s);
}

const char *mt_str(mt_buf *b) { return b->s ? b->s : ""; }

char *mt_take(mt_buf *b) {
    char *s = b->s ? b->s : mt_strdup("");
    *b = (mt_buf){0};
    return s;
}

void mt_buf_free(mt_buf *b) {
    free(b->s);
    *b = (mt_buf){0};
}

void mt_push_own(mt_strs *l, char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 4;
        l->v = mt_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = s;
}

void mt_push(mt_strs *l, const char *s) { mt_push_own(l, mt_strdup(s)); }

void mt_strs_free(mt_strs *l) {
    for (size_t i = 0; i < l->n; i++) free(l->v[i]);
    free(l->v);
    *l = (mt_strs){0};
}

mt_strs mt_strs_clone(const mt_strs *l) {
    mt_strs c = {0};
    for (size_t i = 0; i < l->n; i++) mt_push(&c, l->v[i]);
    return c;
}

bool mt_strs_eq(const mt_strs *a, const mt_strs *b) {
    if (a->n != b->n) return false;
    for (size_t i = 0; i < a->n; i++)
        if (strcmp(a->v[i], b->v[i]) != 0) return false;
    return true;
}

long mt_strs_find(const mt_strs *l, const char *s) {
    for (size_t i = 0; i < l->n; i++)
        if (strcmp(l->v[i], s) == 0) return (long)i;
    return -1;
}

bool mt_strs_has(const mt_strs *l, const char *s) { return mt_strs_find(l, s) >= 0; }

void mt_strs_remove(mt_strs *l, size_t i) {
    free(l->v[i]);
    memmove(l->v + i, l->v + i + 1, (l->n - i - 1) * sizeof *l->v);
    l->n--;
}

char *mt_join(const mt_strs *l, const char *sep) {
    mt_buf b = {0};
    for (size_t i = 0; i < l->n; i++) {
        if (i) mt_puts(&b, sep);
        mt_puts(&b, l->v[i]);
    }
    return mt_take(&b);
}

mt_strs mt_split(const char *s, char c) {
    mt_strs l = {0};
    const char *p = s;
    for (;;) {
        const char *q = strchr(p, c);
        if (!q) {
            mt_push(&l, p);
            return l;
        }
        mt_push_own(&l, mt_strndup(p, (size_t)(q - p)));
        p = q + 1;
    }
}

static bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

mt_strs mt_words(const char *s) {
    mt_strs l = {0};
    const char *p = s;
    while (*p) {
        while (*p && ws(*p)) p++;
        if (!*p) break;
        const char *q = p;
        while (*q && !ws(*q)) q++;
        mt_push_own(&l, mt_strndup(p, (size_t)(q - p)));
        p = q;
    }
    return l;
}

mt_strs mt_lines(const char *s) {
    mt_strs l = {0};
    const char *p = s;
    while (*p) {
        const char *q = strchr(p, '\n');
        size_t n = q ? (size_t)(q - p) : strlen(p);
        size_t k = n && p[n - 1] == '\r' ? n - 1 : n;
        mt_push_own(&l, mt_strndup(p, k));
        if (!q) break;
        p = q + 1;
    }
    return l;
}

bool mt_split_once(const char *s, char c, char **a, char **b) {
    const char *q = strchr(s, c);
    if (!q) return false;
    *a = mt_strndup(s, (size_t)(q - s));
    *b = mt_strdup(q + 1);
    return true;
}

char *mt_trim(const char *s) {
    while (*s && ws(*s)) s++;
    size_t n = strlen(s);
    while (n && ws(s[n - 1])) n--;
    return mt_strndup(s, n);
}
