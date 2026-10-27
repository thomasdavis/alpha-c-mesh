/* The arena: a list of chunks, newest first; big requests get a chunk of their own. */
#include "tui/arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tui_arena_chunk {
    tui_arena_chunk *next;
    size_t cap;
    _Alignas(16) unsigned char data[];
};

enum { CHUNK = 64 * 1024, ALIGN = 16 };

static _Noreturn void oom(void) {
    fputs("tui-arena: out of memory\n", stderr);
    abort();
}

static tui_arena_chunk *chunk_new(size_t cap, tui_arena_chunk *next) {
    tui_arena_chunk *c = malloc(sizeof *c + cap);
    if (!c) oom();
    c->next = next;
    c->cap = cap;
    return c;
}

void *tui_arena_alloc(tui_arena *a, size_t n) {
    size_t need = (n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    if (need == 0) need = ALIGN;
    if (!a->head || a->used + need > a->head->cap) {
        size_t cap = need > CHUNK ? need : CHUNK;
        a->head = chunk_new(cap, a->head);
        a->used = 0;
    }
    void *p = a->head->data + a->used;
    a->used += need;
    memset(p, 0, need);
    return p;
}

void *tui_arena_grow(tui_arena *a, void *old, size_t old_n, size_t new_n) {
    if (new_n <= old_n) return old;
    size_t old_need = (old_n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    size_t new_need = (new_n + ALIGN - 1) & ~(size_t)(ALIGN - 1);
    /* The last allocation of the head chunk grows in place when it fits. */
    if (old && a->head && (unsigned char *)old + old_need == a->head->data + a->used &&
        a->used - old_need + new_need <= a->head->cap) {
        memset((unsigned char *)old + old_need, 0, new_need - old_need);
        a->used += new_need - old_need;
        return old;
    }
    void *p = tui_arena_alloc(a, new_n);
    if (old && old_n) memcpy(p, old, old_n);
    return p;
}

char *tui_arena_strndup(tui_arena *a, const char *s, size_t n) {
    char *d = tui_arena_alloc(a, n + 1);
    if (n) memcpy(d, s, n);
    return d;
}

char *tui_arena_strdup(tui_arena *a, const char *s) { return tui_arena_strndup(a, s, strlen(s)); }

char *tui_arena_vfmt(tui_arena *a, const char *fmt, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (n < 0) return tui_arena_alloc(a, 1);
    char *s = tui_arena_alloc(a, (size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    return s;
}

char *tui_arena_fmt(tui_arena *a, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *s = tui_arena_vfmt(a, fmt, ap);
    va_end(ap);
    return s;
}

void tui_arena_reset(tui_arena *a) {
    if (!a->head) return;
    tui_arena_chunk *keep = a->head;
    while (keep->next) keep = keep->next; /* the oldest chunk is the first, small one */
    tui_arena_chunk *c = a->head;
    while (c != keep) {
        tui_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    keep->next = NULL;
    a->head = keep;
    a->used = 0;
}

void tui_arena_free(tui_arena *a) {
    tui_arena_chunk *c = a->head;
    while (c) {
        tui_arena_chunk *next = c->next;
        free(c);
        c = next;
    }
    a->head = NULL;
    a->used = 0;
}
