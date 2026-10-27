/* @ajax/tui-arena: a bump allocator. A terminal frame builds many short-lived strings, spans and
 * lines; they all come from one arena and go with one tui_arena_reset (or tui_arena_free). Memory
 * never fails visibly: an allocation failure aborts. */
#ifndef TUI_ARENA_H
#define TUI_ARENA_H

#include <stdarg.h>
#include <stddef.h>

typedef struct tui_arena_chunk tui_arena_chunk;

typedef struct {
    tui_arena_chunk *head; /* the chunk being filled; older chunks follow */
    size_t used;           /* bytes used in head */
} tui_arena;

/* Zeroed, 16-byte aligned memory that lives until the arena is reset or freed. */
void *tui_arena_alloc(tui_arena *a, size_t n);
/* Grows the last allocation (or copies when it isn't the last). old may be NULL. */
void *tui_arena_grow(tui_arena *a, void *old, size_t old_n, size_t new_n);
char *tui_arena_strdup(tui_arena *a, const char *s);
char *tui_arena_strndup(tui_arena *a, const char *s, size_t n);
char *tui_arena_fmt(tui_arena *a, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;
char *tui_arena_vfmt(tui_arena *a, const char *fmt, va_list ap);
/* Forgets every allocation but keeps the first chunk for reuse. */
void tui_arena_reset(tui_arena *a);
void tui_arena_free(tui_arena *a);

#endif
