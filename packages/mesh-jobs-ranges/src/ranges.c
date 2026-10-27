/* jobs.rs's Ranges over a sorted array of [start, end) pairs: disjoint and never touching. */
#include "mesh/jobs/ranges.h"
#include "mesh/debug_str.h"

#include <stdlib.h>
#include <string.h>

static void grow(mj_ranges *r, size_t need) {
    if (need <= r->cap) return;
    size_t cap = r->cap ? r->cap * 2 : 4;
    while (cap < need) cap *= 2;
    r->v = mxrealloc(r->v, cap * sizeof *r->v);
    r->cap = cap;
}

mj_ranges mj_ranges_of(uint64_t s, uint64_t e) {
    mj_ranges r = {0};
    mj_ranges_insert(&r, s, e);
    return r;
}

void mj_ranges_free(mj_ranges *r) {
    free(r->v);
    *r = (mj_ranges){0};
}

void mj_ranges_clear(mj_ranges *r) { r->n = 0; }

mj_ranges mj_ranges_clone(const mj_ranges *r) {
    mj_ranges c = {0};
    if (r->n) {
        grow(&c, r->n);
        memcpy(c.v, r->v, r->n * sizeof *r->v);
        c.n = r->n;
    }
    return c;
}

bool mj_ranges_eq(const mj_ranges *a, const mj_ranges *b) {
    return a->n == b->n && (a->n == 0 || memcmp(a->v, b->v, a->n * sizeof *a->v) == 0);
}

uint64_t mj_ranges_len(const mj_ranges *r) {
    uint64_t n = 0;
    for (size_t i = 0; i < r->n; i++) {
        uint64_t d = r->v[i][1] - r->v[i][0];
        n = n > UINT64_MAX - d ? UINT64_MAX : n + d;
    }
    return n;
}

bool mj_ranges_first(const mj_ranges *r, uint64_t *x) {
    if (!r->n) return false;
    *x = r->v[0][0];
    return true;
}

bool mj_ranges_end(const mj_ranges *r, uint64_t *e) {
    if (!r->n) return false;
    *e = r->v[r->n - 1][1];
    return true;
}

/* The index of the last range starting at or before x, or -1. */
static long floor_at(const mj_ranges *r, uint64_t x) {
    size_t lo = 0, hi = r->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (r->v[mid][0] <= x) lo = mid + 1;
        else hi = mid;
    }
    return (long)lo - 1;
}

/* The index of the last range starting before x, or -1. */
static long below(const mj_ranges *r, uint64_t x) {
    size_t lo = 0, hi = r->n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (r->v[mid][0] < x) lo = mid + 1;
        else hi = mid;
    }
    return (long)lo - 1;
}

bool mj_ranges_contains(const mj_ranges *r, uint64_t x) {
    long i = floor_at(r, x);
    return i >= 0 && x < r->v[i][1];
}

void mj_ranges_insert(mj_ranges *r, uint64_t s, uint64_t e) {
    if (s >= e) return;
    /* Every range starting at or before e that reaches s (touching included) merges in. */
    long hi = floor_at(r, e), lo = hi;
    while (lo >= 0 && r->v[lo][1] >= s) {
        if (r->v[lo][0] < s) s = r->v[lo][0];
        if (r->v[lo][1] > e) e = r->v[lo][1];
        lo--;
    }
    size_t first = (size_t)(lo + 1), gone = (size_t)(hi - lo);
    if (gone == 0) {
        grow(r, r->n + 1);
        memmove(&r->v[first + 1], &r->v[first], (r->n - first) * sizeof *r->v);
        r->n++;
    } else if (gone > 1) {
        memmove(&r->v[first + 1], &r->v[first + gone], (r->n - first - gone) * sizeof *r->v);
        r->n -= gone - 1;
    }
    r->v[first][0] = s;
    r->v[first][1] = e;
}

void mj_ranges_add(mj_ranges *r, uint64_t x) { mj_ranges_insert(r, x, x == UINT64_MAX ? x : x + 1); }

void mj_ranges_union(mj_ranges *r, const mj_ranges *o) {
    if (r == o) return;
    for (size_t i = 0; i < o->n; i++) mj_ranges_insert(r, o->v[i][0], o->v[i][1]);
}

bool mj_ranges_remove(mj_ranges *r, uint64_t s, uint64_t e) {
    if (s >= e) return false;
    /* Every range starting before e that ends after s. */
    long hi = below(r, e), lo = hi;
    while (lo >= 0 && r->v[lo][1] > s) lo--;
    size_t first = (size_t)(lo + 1), gone = (size_t)(hi - lo);
    if (gone == 0) return false;
    uint64_t keep[2][2];
    size_t nkeep = 0;
    if (r->v[first][0] < s) {
        keep[nkeep][0] = r->v[first][0];
        keep[nkeep++][1] = s;
    }
    if (r->v[first + gone - 1][1] > e) {
        keep[nkeep][0] = e;
        keep[nkeep++][1] = r->v[first + gone - 1][1];
    }
    size_t tail = r->n - first - gone;
    grow(r, r->n - gone + nkeep);
    memmove(&r->v[first + nkeep], &r->v[first + gone], tail * sizeof *r->v);
    memcpy(&r->v[first], keep, nkeep * sizeof *r->v);
    r->n = r->n - gone + nkeep;
    return true;
}

bool mj_ranges_take(mj_ranges *r, uint64_t x) { return mj_ranges_remove(r, x, x == UINT64_MAX ? x : x + 1); }

void mj_ranges_subtract(mj_ranges *r, const mj_ranges *o) {
    if (r == o) {
        r->n = 0;
        return;
    }
    for (size_t i = 0; i < o->n; i++) mj_ranges_remove(r, o->v[i][0], o->v[i][1]);
}

mj_ranges mj_ranges_intersect(const mj_ranges *a, const mj_ranges *b) {
    mj_ranges r = {0};
    for (size_t i = 0; i < a->n; i++) {
        uint64_t s = a->v[i][0], e = a->v[i][1];
        for (long j = below(b, e); j >= 0 && b->v[j][1] > s; j--)
            mj_ranges_insert(&r, s > b->v[j][0] ? s : b->v[j][0], e < b->v[j][1] ? e : b->v[j][1]);
    }
    return r;
}

mj_ranges mj_ranges_take_front(mj_ranges *r, uint64_t n) {
    mj_ranges got = {0};
    uint64_t left = n;
    while (left > 0 && r->n) {
        uint64_t s = r->v[0][0], e = r->v[0][1];
        uint64_t k = e - s < left ? e - s : left;
        mj_ranges_insert(&got, s, s + k);
        mj_ranges_remove(r, s, s + k);
        left -= k;
    }
    return got;
}

bool mj_ranges_nth(const mj_ranges *r, uint64_t k, uint64_t *x) {
    uint64_t left = k;
    for (size_t i = 0; i < r->n; i++) {
        uint64_t d = r->v[i][1] - r->v[i][0];
        if (left < d) {
            *x = r->v[i][0] + left;
            return true;
        }
        left -= d;
    }
    return false;
}

void mj_ranges_word(const mj_ranges *r, msb *out) {
    if (!r->n) {
        msb_putc(out, '-');
        return;
    }
    for (size_t i = 0; i < r->n; i++) {
        if (i) msb_putc(out, ',');
        if (r->v[i][1] == UINT64_MAX) msb_printf(out, "%llu-", (unsigned long long)r->v[i][0]);
        else msb_printf(out, "%llu-%llu", (unsigned long long)r->v[i][0], (unsigned long long)r->v[i][1]);
    }
}

char *mj_ranges_word_new(const mj_ranges *r) {
    msb b = {0};
    mj_ranges_word(r, &b);
    return msb_take(&b);
}

/* Rust's `x.replace('_', "").parse::<u64>()`: an optional '+', then decimal digits, no overflow. */
static bool num(const char *s, size_t n, uint64_t *out) {
    uint64_t v = 0;
    size_t digits = 0, i = 0;
    /* Underscores vanish first, so "+_1" is "+1". */
    while (i < n && s[i] == '_') i++;
    if (i < n && s[i] == '+') i++;
    for (; i < n; i++) {
        char c = s[i];
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        uint64_t d = (uint64_t)(c - '0');
        if (v > (UINT64_MAX - d) / 10) return false;
        v = v * 10 + d;
        digits++;
    }
    if (!digits) return false;
    *out = v;
    return true;
}

static bool bad(const char *s, char **err) {
    if (err) {
        msb b = {0};
        msb_puts(&b, "items ");
        mdebug_str(&b, s, strlen(s));
        msb_puts(&b, ": `<n>` (0..n), or `a-b` (half-open), `a-` (endless), `a`, comma-separated");
        *err = msb_take(&b);
    }
    return false;
}

bool mj_ranges_parse(const char *s, mj_ranges *out, char **err) {
    mj_ranges r = {0};
    if (strcmp(s, "-") == 0) {
        *out = r;
        return true;
    }
    const char *p = s;
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t len = comma ? (size_t)(comma - p) : strlen(p);
        const char *dash = memchr(p, '-', len);
        uint64_t a, b;
        if (dash) {
            size_t la = (size_t)(dash - p), lb = len - la - 1;
            if (!num(p, la, &a)) goto fail;
            if (lb == 0) mj_ranges_insert(&r, a, UINT64_MAX);
            else {
                if (!num(dash + 1, lb, &b) || a >= b) goto fail;
                mj_ranges_insert(&r, a, b);
            }
        } else {
            if (!num(p, len, &a)) goto fail;
            mj_ranges_add(&r, a);
        }
        if (!comma) break;
        p = comma + 1;
    }
    *out = r;
    return true;
fail:
    mj_ranges_free(&r);
    return bad(s, err);
}
