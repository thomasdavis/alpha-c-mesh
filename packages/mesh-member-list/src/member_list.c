/* Member lists and masks (see the header). */
#include "mesh/member_list.h"
#include "mesh/abi/base.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

void ml_push(ml_list *l, uint32_t k) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->v = mt_realloc(l->v, l->cap * sizeof *l->v);
    }
    l->v[l->n++] = k;
}

long ml_find(const ml_list *l, uint32_t k) {
    for (size_t i = 0; i < l->n; i++)
        if (l->v[i] == k) return (long)i;
    return -1;
}

bool ml_has(const ml_list *l, uint32_t k) { return ml_find(l, k) >= 0; }

void ml_remove_at(ml_list *l, size_t i) {
    memmove(l->v + i, l->v + i + 1, (l->n - i - 1) * sizeof *l->v);
    l->n--;
}

void ml_remove_value(ml_list *l, uint32_t k) {
    size_t j = 0;
    for (size_t i = 0; i < l->n; i++)
        if (l->v[i] != k) l->v[j++] = l->v[i];
    l->n = j;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

void ml_sort(ml_list *l) {
    if (l->n > 1) qsort(l->v, l->n, sizeof *l->v, cmp_u32);
}

bool ml_eq(const ml_list *a, const ml_list *b) {
    return a->n == b->n && (a->n == 0 || memcmp(a->v, b->v, a->n * sizeof *a->v) == 0);
}

ml_list ml_of(const uint32_t *v, size_t n) {
    ml_list l = {0};
    for (size_t i = 0; i < n; i++) ml_push(&l, v[i]);
    return l;
}

ml_list ml_clone(const ml_list *l) { return ml_of(l->v, l->n); }

void ml_free(ml_list *l) {
    free(l->v);
    *l = (ml_list){0};
}

ml_opt ml_none(void) { return (ml_opt){0}; }
ml_opt ml_some(const uint32_t *v, size_t n) { return (ml_opt){true, ml_of(v, n)}; }

bool ml_opt_eq(const ml_opt *a, const ml_opt *b) {
    if (a->some != b->some) return false;
    return !a->some || ml_eq(&a->l, &b->l);
}

ml_opt ml_opt_clone(const ml_opt *o) { return o->some ? (ml_opt){true, ml_clone(&o->l)} : ml_none(); }

void ml_opt_free(ml_opt *o) {
    ml_free(&o->l);
    o->some = false;
}

bool ml_opt_has_or_all(const ml_opt *o, uint32_t k) { return !o->some || ml_has(&o->l, k); }

char *ml_text(const ml_list *l) {
    if (l->n == 0) return mt_strdup("-");
    mt_buf b = {0};
    for (size_t i = 0; i < l->n; i++) mt_printf(&b, i ? ",%u" : "%u", l->v[i]);
    return mt_take(&b);
}

char *ml_at_word(const ml_opt *o) { return o->some ? ml_text(&o->l) : mt_strdup("all"); }

bool ml_parse(const char *s, ml_list *out, char **err) {
    *out = (ml_list){0};
    if (strcmp(s, "-") == 0) return true;
    mt_strs w = mt_split(s, ',');
    for (size_t i = 0; i < w.n; i++) {
        uint32_t k;
        if (!mp_u32(w.v[i], &k)) {
            char *q = rf_debug_str(s);
            *err = mt_fmt("bad member list %s", q);
            free(q);
            goto fail;
        }
        if (k >= MESH_MAX_SYSTEMS) {
            *err = mt_fmt("member %u >= %u", k, (unsigned)MESH_MAX_SYSTEMS);
            goto fail;
        }
        ml_push(out, k);
    }
    mt_strs_free(&w);
    return true;
fail:
    mt_strs_free(&w);
    ml_free(out);
    return false;
}

uint32_t ml_mask(const ml_list *l) {
    uint32_t m = 0;
    for (size_t i = 0; i < l->n; i++) m |= 1u << l->v[i];
    return m;
}

ml_list ml_members(uint32_t mask) {
    ml_list l = {0};
    for (uint32_t k = 0; k < 32; k++)
        if (mask >> k & 1) ml_push(&l, k);
    return l;
}

int ml_popcount(uint32_t x) {
    int n = 0;
    for (; x; x &= x - 1) n++;
    return n;
}
