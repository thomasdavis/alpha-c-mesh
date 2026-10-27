/* @ajax/range-set: a set of u64 items as disjoint, non-touching half-open ranges [s, e), sorted.
 * Inserting merges what overlaps or touches; removing splits. Lengths saturate (an endless range is
 * ~u64::MAX items). */
#ifndef MESH_RANGE_SET_H
#define MESH_RANGE_SET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t *s, *e;
    size_t n, cap;
} rs_set;

rs_set rs_of(uint64_t s, uint64_t e);
bool rs_is_empty(const rs_set *r);
uint64_t rs_len(const rs_set *r); /* items, saturating */
size_t rs_count(const rs_set *r); /* ranges */
bool rs_first(const rs_set *r, uint64_t *x);
bool rs_end(const rs_set *r, uint64_t *x);
bool rs_contains(const rs_set *r, uint64_t x);
void rs_insert(rs_set *r, uint64_t s, uint64_t e);
void rs_add(rs_set *r, uint64_t x);
void rs_union(rs_set *r, const rs_set *o);
/* Removes [s, e); true if anything was in it. */
bool rs_remove(rs_set *r, uint64_t s, uint64_t e);
bool rs_take(rs_set *r, uint64_t x);
void rs_subtract(rs_set *r, const rs_set *o);
bool rs_eq(const rs_set *a, const rs_set *b);
rs_set rs_clone(const rs_set *r);
void rs_free(rs_set *r);

#endif
