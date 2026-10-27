/* @ajax/mesh-omap: an ordered map from fixed-width keys (1..6 u64 words, compared word by word,
 * first word first) to fixed-size values, kept in an AVL tree. It stands in for Rust's BTreeMap
 * wherever the port needs its order: iteration, the first and last entries, and range queries
 * (floor, ceiling, next, previous).
 *
 * Values live inside the tree's nodes; a pointer to one is valid until the map next changes.
 * Iterating by key (first, then next after each key) stays correct when the loop removes or adds
 * entries, which is how the port writes Rust's `retain` and `range(..).rev()` loops. */
#ifndef MESH_OMAP_H
#define MESH_OMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOMAP_MAX_KEY 6

typedef struct momap momap;
/* Releases what a value owns (not the value's own bytes). */
typedef void (*momap_free_fn)(void *val);
/* Copies src into dst (dst is uninitialised), deeply. */
typedef void (*momap_copy_fn)(void *dst, const void *src);

/* free_val and copy_val may be NULL: nothing to release, and a plain byte copy. */
momap *momap_new(unsigned key_words, size_t val_size, momap_free_fn free_val, momap_copy_fn copy_val);
void momap_free(momap *m); /* NULL is fine */
void momap_clear(momap *m);
momap *momap_clone(const momap *m);
size_t momap_len(const momap *m);
unsigned momap_key_words(const momap *m);
size_t momap_val_size(const momap *m);

/* The value under key, or NULL. */
void *momap_get(const momap *m, const uint64_t *key);
/* The value under key, inserted (zeroed) if absent; *fresh (if fresh) says which. */
void *momap_put(momap *m, const uint64_t *key, bool *fresh);
/* Removes key's entry, releasing its value; false if absent. */
bool momap_del(momap *m, const uint64_t *key);
/* Removes key's entry and moves its value into out (not released); false if absent. */
bool momap_take(momap *m, const uint64_t *key, void *out);

/* Navigation: the entry's value, its key copied into key_out (when not NULL), or NULL. */
void *momap_first(const momap *m, uint64_t *key_out);
void *momap_last(const momap *m, uint64_t *key_out);
void *momap_next(const momap *m, const uint64_t *after, uint64_t *key_out);   /* smallest > after */
void *momap_prev(const momap *m, const uint64_t *before, uint64_t *key_out);  /* largest < before */
void *momap_floor(const momap *m, const uint64_t *at, uint64_t *key_out);     /* largest <= at */
void *momap_ceil(const momap *m, const uint64_t *at, uint64_t *key_out);      /* smallest >= at */

/* A cursor over every entry in key order, O(1) per step: for walks that don't change the map's keys
 * (values may change). momap_iter_init then, while momap_iter_next gives a value, use it. */
typedef struct {
    const void *stack[96];
    int top;
    const momap *m;
} momap_iter;
void momap_iter_init(momap_iter *it, const momap *m);
/* The next entry's value (its key in *key when key is not NULL), or NULL at the end. */
void *momap_iter_next(momap_iter *it, const uint64_t **key);
/* The same, copying a one-word key into *key. */
void *momap_iter_next1(momap_iter *it, uint64_t *key);

/* Moves every entry with key >= at into a new map (Rust's split_off). */
momap *momap_split_off(momap *m, const uint64_t *at);

/* One-word keys. */
static inline void *momap_get1(const momap *m, uint64_t k) { return momap_get(m, &k); }
static inline void *momap_put1(momap *m, uint64_t k, bool *fresh) { return momap_put(m, &k, fresh); }
static inline bool momap_del1(momap *m, uint64_t k) { return momap_del(m, &k); }

#endif
