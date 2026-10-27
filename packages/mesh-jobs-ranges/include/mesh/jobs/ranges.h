/* @ajax/mesh-jobs-ranges: a set of u64 items (a job's items) as disjoint, non-touching half-open
 * ranges [s, e), in order. An endless range ends at UINT64_MAX. jobs.rs's `Ranges`.
 *
 * Kept as a sorted array of pairs; a zeroed mj_ranges is the empty set. */
#ifndef MESH_JOBS_RANGES_H
#define MESH_JOBS_RANGES_H

#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t (*v)[2]; /* [start, end) */
    size_t n, cap;
} mj_ranges;

mj_ranges mj_ranges_of(uint64_t s, uint64_t e);
void mj_ranges_free(mj_ranges *r);
mj_ranges mj_ranges_clone(const mj_ranges *r);
void mj_ranges_clear(mj_ranges *r);
bool mj_ranges_eq(const mj_ranges *a, const mj_ranges *b);

static inline bool mj_ranges_is_empty(const mj_ranges *r) { return r->n == 0; }
/* Items in the set (saturating: an endless range is ~UINT64_MAX). */
uint64_t mj_ranges_len(const mj_ranges *r);
/* Ranges in the set. */
static inline size_t mj_ranges_count(const mj_ranges *r) { return r->n; }
/* The first item; false when empty. */
bool mj_ranges_first(const mj_ranges *r, uint64_t *x);
/* The end of the last range; false when empty. */
bool mj_ranges_end(const mj_ranges *r, uint64_t *e);
bool mj_ranges_contains(const mj_ranges *r, uint64_t x);

void mj_ranges_insert(mj_ranges *r, uint64_t s, uint64_t e);
void mj_ranges_add(mj_ranges *r, uint64_t x);
void mj_ranges_union(mj_ranges *r, const mj_ranges *o);
/* Removes [s, e); true if anything was in it. */
bool mj_ranges_remove(mj_ranges *r, uint64_t s, uint64_t e);
bool mj_ranges_take(mj_ranges *r, uint64_t x);
void mj_ranges_subtract(mj_ranges *r, const mj_ranges *o);
/* The items of both. */
mj_ranges mj_ranges_intersect(const mj_ranges *a, const mj_ranges *b);
/* Takes up to n items from the front. */
mj_ranges mj_ranges_take_front(mj_ranges *r, uint64_t n);
/* The item with k items of the set before it; false when there are fewer than k + 1. */
bool mj_ranges_nth(const mj_ranges *r, uint64_t k, uint64_t *x);

/* `a-b,c-d` (half-open; `a-` is endless), `-` when empty. */
void mj_ranges_word(const mj_ranges *r, msb *out);
char *mj_ranges_word_new(const mj_ranges *r);
/* The `word` form, or items `a`, `a-b`, `a-` (endless) comma-separated. On error returns false
 * with *err (the caller frees it). */
bool mj_ranges_parse(const char *s, mj_ranges *out, char **err);

#endif
