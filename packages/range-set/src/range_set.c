/* jobs.rs Ranges, as a sorted array. */
#include "mesh/range_set.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

static void insert_at(rs_set *r, size_t i, uint64_t s, uint64_t e) {
    if (r->n == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 8;
        r->s = mt_realloc(r->s, r->cap * sizeof *r->s);
        r->e = mt_realloc(r->e, r->cap * sizeof *r->e);
    }
    memmove(r->s + i + 1, r->s + i, (r->n - i) * sizeof *r->s);
    memmove(r->e + i + 1, r->e + i, (r->n - i) * sizeof *r->e);
    r->s[i] = s, r->e[i] = e;
    r->n++;
}

static void remove_range(rs_set *r, size_t i, size_t j) {
    if (i == j) return;
    memmove(r->s + i, r->s + j, (r->n - j) * sizeof *r->s);
    memmove(r->e + i, r->e + j, (r->n - j) * sizeof *r->e);
    r->n -= j - i;
}

rs_set rs_of(uint64_t s, uint64_t e) {
    rs_set r = {0};
    rs_insert(&r, s, e);
    return r;
}

bool rs_is_empty(const rs_set *r) { return r->n == 0; }

uint64_t rs_len(const rs_set *r) {
    uint64_t n = 0;
    for (size_t i = 0; i < r->n; i++) {
        uint64_t d = r->e[i] - r->s[i];
        n = n > UINT64_MAX - d ? UINT64_MAX : n + d;
    }
    return n;
}

size_t rs_count(const rs_set *r) { return r->n; }
bool rs_first(const rs_set *r, uint64_t *x) { return r->n ? (*x = r->s[0], true) : false; }
bool rs_end(const rs_set *r, uint64_t *x) { return r->n ? (*x = r->e[r->n - 1], true) : false; }

bool rs_contains(const rs_set *r, uint64_t x) {
    for (size_t i = r->n; i-- > 0;)
        if (r->s[i] <= x) return x < r->e[i];
    return false;
}

void rs_insert(rs_set *r, uint64_t s, uint64_t e) {
    if (s >= e) return;
    /* Every range that overlaps or touches [s, e) merges into it. */
    size_t i = 0;
    while (i < r->n && r->e[i] < s) i++;
    size_t j = i;
    while (j < r->n && r->s[j] <= e) {
        if (r->s[j] < s) s = r->s[j];
        if (r->e[j] > e) e = r->e[j];
        j++;
    }
    remove_range(r, i, j);
    insert_at(r, i, s, e);
}

void rs_add(rs_set *r, uint64_t x) { rs_insert(r, x, x == UINT64_MAX ? x : x + 1); }

void rs_union(rs_set *r, const rs_set *o) {
    for (size_t i = 0; i < o->n; i++) rs_insert(r, o->s[i], o->e[i]);
}

bool rs_remove(rs_set *r, uint64_t s, uint64_t e) {
    if (s >= e) return false;
    bool any = false;
    for (size_t i = 0; i < r->n;) {
        uint64_t a = r->s[i], b = r->e[i];
        if (b <= s || a >= e) {
            i++;
            continue;
        }
        any = true;
        remove_range(r, i, i + 1);
        if (a < s) insert_at(r, i++, a, s);
        if (b > e) insert_at(r, i++, e, b);
    }
    return any;
}

bool rs_take(rs_set *r, uint64_t x) { return rs_remove(r, x, x == UINT64_MAX ? x : x + 1); }

void rs_subtract(rs_set *r, const rs_set *o) {
    for (size_t i = 0; i < o->n; i++) rs_remove(r, o->s[i], o->e[i]);
}

bool rs_eq(const rs_set *a, const rs_set *b) {
    return a->n == b->n && (!a->n || (!memcmp(a->s, b->s, a->n * sizeof *a->s) && !memcmp(a->e, b->e, a->n * sizeof *a->e)));
}

rs_set rs_clone(const rs_set *r) {
    rs_set c = {0};
    for (size_t i = 0; i < r->n; i++) insert_at(&c, i, r->s[i], r->e[i]);
    return c;
}

void rs_free(rs_set *r) {
    free(r->s);
    free(r->e);
    *r = (rs_set){0};
}
