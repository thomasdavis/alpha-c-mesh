/* mesh-omap against a plain sorted array: random puts, deletes, takes, navigation, clones and
 * split-offs, with one- and three-word keys; values that own memory are released exactly once. */
#include "mesh/omap.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

static uint64_t rng = 88172645463325252u;
static uint64_t rnd(uint64_t n) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng % n;
}

static long live; /* boxed values alive */
static void box_free(void *v) {
    free(*(uint64_t **)v);
    live--;
}
static void box_copy(void *dst, const void *src) {
    uint64_t *b = malloc(sizeof *b);
    *b = **(uint64_t *const *)src;
    *(uint64_t **)dst = b;
    live++;
}

/* The reference: keys and values in a sorted array (keys of kw words). */
typedef struct {
    uint64_t k[3];
    uint64_t v;
} ent;
static ent ref[4096];
static size_t nref;
static unsigned kw;

static int kcmp(const uint64_t *a, const uint64_t *b) {
    for (unsigned i = 0; i < kw; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
static long ref_find(const uint64_t *k) {
    for (size_t i = 0; i < nref; i++)
        if (kcmp(ref[i].k, k) == 0) return (long)i;
    return -1;
}
static void ref_put(const uint64_t *k, uint64_t v) {
    long i = ref_find(k);
    if (i >= 0) {
        ref[i].v = v;
        return;
    }
    size_t at = 0;
    while (at < nref && kcmp(ref[at].k, k) < 0) at++;
    memmove(&ref[at + 1], &ref[at], (nref - at) * sizeof(ent));
    memset(&ref[at], 0, sizeof(ent));
    memcpy(ref[at].k, k, 8 * kw);
    ref[at].v = v;
    nref++;
}
static void ref_del(const uint64_t *k) {
    long i = ref_find(k);
    if (i < 0) return;
    memmove(&ref[i], &ref[i + 1], (nref - (size_t)i - 1) * sizeof(ent));
    nref--;
}

static void key(uint64_t *k) {
    for (unsigned i = 0; i < kw; i++) k[i] = rnd(kw == 1 ? 600 : 6);
}

static void same(const momap *m) {
    CHECK_U(momap_len(m), nref);
    uint64_t k[3];
    size_t i = 0;
    for (uint64_t **v = momap_first(m, k); v; v = momap_next(m, k, k), i++) {
        if (!CHECK(i < nref)) return;
        CHECK(kcmp(k, ref[i].k) == 0);
        CHECK_U(**v, ref[i].v);
    }
    CHECK_U(i, nref);
    /* The cursor walks the same entries in the same order. */
    momap_iter it;
    momap_iter_init(&it, m);
    const uint64_t *ik;
    i = 0;
    for (uint64_t **v = momap_iter_next(&it, &ik); v; v = momap_iter_next(&it, &ik), i++) {
        if (!CHECK(i < nref)) break;
        CHECK(kcmp(ik, ref[i].k) == 0 && **v == ref[i].v);
    }
    CHECK_U(i, nref);
    i = nref;
    for (uint64_t **v = momap_last(m, k); v; v = momap_prev(m, k, k)) {
        i--;
        CHECK(kcmp(k, ref[i].k) == 0);
    }
    CHECK_U(i, 0);
}

static void run(unsigned words) {
    kw = words;
    nref = 0;
    momap *m = momap_new(kw, sizeof(uint64_t *), box_free, box_copy);
    for (int step = 0; step < 6000; step++) {
        uint64_t k[3] = {0, 0, 0};
        key(k);
        switch (rnd(6)) {
        case 0:
        case 1:
        case 2: {
            bool fresh;
            uint64_t **v = momap_put(m, k, &fresh);
            CHECK(fresh == (ref_find(k) < 0));
            if (fresh) {
                *v = malloc(sizeof **v);
                live++;
            }
            **v = (uint64_t)step;
            ref_put(k, (uint64_t)step);
            break;
        }
        case 3: CHECK(momap_del(m, k) == (ref_find(k) >= 0)); ref_del(k); break;
        case 4: {
            uint64_t *out = NULL;
            bool had = ref_find(k) >= 0;
            CHECK(momap_take(m, k, &out) == had);
            if (had) {
                CHECK_U(*out, ref[ref_find(k)].v);
                free(out);
                live--;
            }
            ref_del(k);
            break;
        }
        default: {
            /* Navigation around k. */
            uint64_t got[3];
            long fl = -1, ce = -1, nx = -1, pv = -1;
            for (size_t i = 0; i < nref; i++) {
                int c = kcmp(ref[i].k, k);
                if (c <= 0) fl = (long)i;
                if (c < 0) pv = (long)i;
                if (c >= 0 && ce < 0) ce = (long)i;
                if (c > 0 && nx < 0) nx = (long)i;
            }
            uint64_t **v = momap_floor(m, k, got);
            CHECK((v != NULL) == (fl >= 0));
            if (v && fl >= 0) CHECK(kcmp(got, ref[fl].k) == 0);
            v = momap_ceil(m, k, got);
            CHECK((v != NULL) == (ce >= 0));
            if (v && ce >= 0) CHECK(kcmp(got, ref[ce].k) == 0);
            v = momap_next(m, k, got);
            CHECK((v != NULL) == (nx >= 0));
            if (v && nx >= 0) CHECK(kcmp(got, ref[nx].k) == 0);
            v = momap_prev(m, k, got);
            CHECK((v != NULL) == (pv >= 0));
            if (v && pv >= 0) CHECK(kcmp(got, ref[pv].k) == 0);
        }
        }
        if (step % 500 == 0) same(m);
    }
    same(m);
    CHECK_I(live, (long)nref);
    /* A clone is deep and equal; changing it leaves the original alone. */
    momap *c = momap_clone(m);
    CHECK_I(live, 2 * (long)nref);
    same(c);
    momap_clear(c);
    CHECK_U(momap_len(c), 0);
    same(m);
    momap_free(c);
    /* split_off: everything >= at moves. */
    uint64_t at[3] = {300, 3, 3};
    size_t below = 0;
    for (size_t i = 0; i < nref; i++)
        if (kcmp(ref[i].k, at) < 0) below++;
    momap *hi = momap_split_off(m, at);
    CHECK_U(momap_len(m), below);
    CHECK_U(momap_len(hi), nref - below);
    uint64_t k[3];
    if (momap_len(hi)) {
        CHECK(momap_first(hi, k) != NULL);
        CHECK(kcmp(k, at) >= 0);
    }
    if (momap_len(m)) {
        CHECK(momap_last(m, k) != NULL);
        CHECK(kcmp(k, at) < 0);
    }
    momap_free(hi);
    momap_free(m);
    CHECK_I(live, 0);
}

int main(void) {
    run(1);
    run(3);
    /* Plain values: no callbacks, byte copies. */
    momap *m = momap_new(1, sizeof(uint32_t), NULL, NULL);
    for (uint32_t i = 0; i < 1000; i++) *(uint32_t *)momap_put1(m, (i * 7919u) % 1000, NULL) = i;
    CHECK_U(momap_len(m), 1000);
    uint64_t k = 0;
    uint32_t n = 0;
    for (uint32_t *v = momap_first(m, &k); v; v = momap_next(m, &k, &k)) CHECK_U(k, n++);
    momap *c = momap_clone(m);
    for (uint64_t i = 0; i < 1000; i += 2) CHECK(momap_del1(c, i));
    CHECK_U(momap_len(c), 500);
    CHECK_U(momap_len(m), 1000);
    CHECK(momap_get1(c, 2) == NULL && momap_get1(m, 2) != NULL);
    momap_free(c);
    momap_free(m);
    return mesh_check_done();
}
