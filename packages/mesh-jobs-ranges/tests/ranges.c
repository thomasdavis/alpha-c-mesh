/* jobs.rs's `ranges` test, ported, and the set operations against a bitset over random operations. */
#include "mesh/jobs/ranges.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

static void word_is(const mj_ranges *r, const char *want) {
    char *w = mj_ranges_word_new(r);
    CHECK_S(w, want);
    free(w);
}

static mj_ranges parsed(const char *s) {
    mj_ranges r = {0};
    char *err = NULL;
    if (!mj_ranges_parse(s, &r, &err)) {
        CHECK(!"parse");
        free(err);
    }
    return r;
}

static void the_rust_test(void) {
    mj_ranges r = {0};
    mj_ranges_insert(&r, 10, 20);
    mj_ranges_insert(&r, 30, 40);
    mj_ranges_insert(&r, 20, 25);
    word_is(&r, "10-25,30-40");
    mj_ranges_insert(&r, 24, 31);
    word_is(&r, "10-40");
    CHECK(mj_ranges_remove(&r, 15, 17));
    word_is(&r, "10-15,17-40");
    CHECK(!mj_ranges_remove(&r, 15, 17));
    CHECK_U(mj_ranges_len(&r), 28);
    CHECK(mj_ranges_contains(&r, 14) && !mj_ranges_contains(&r, 15) && mj_ranges_contains(&r, 17) && !mj_ranges_contains(&r, 40));
    uint64_t x;
    CHECK(mj_ranges_nth(&r, 0, &x) && x == 10);
    CHECK(mj_ranges_nth(&r, 5, &x) && x == 17);
    CHECK(!mj_ranges_nth(&r, 28, &x));
    mj_ranges f = mj_ranges_take_front(&r, 7);
    word_is(&f, "10-15,17-19");
    word_is(&r, "19-40");
    mj_ranges hundred = mj_ranges_of(0, 100), p = parsed("5-10,50,99-200");
    mj_ranges i = mj_ranges_intersect(&hundred, &p);
    word_is(&i, "5-10,50-51,99-100");
    mj_ranges e7 = parsed("7-");
    CHECK_U(mj_ranges_len(&e7), UINT64_MAX - 7);
    word_is(&e7, "7-");
    mj_ranges junk = {0};
    char *err = NULL;
    CHECK(!mj_ranges_parse("5-5", &junk, &err));
    CHECK_S(err, "items \"5-5\": `<n>` (0..n), or `a-b` (half-open), `a-` (endless), `a`, comma-separated");
    free(err);
    err = NULL;
    CHECK(!mj_ranges_parse("x", &junk, &err));
    free(err);
    mj_ranges e = parsed("0-");
    mj_ranges t = mj_ranges_take_front(&e, 3);
    word_is(&t, "0-3");
    word_is(&e, "3-");
    mj_ranges dash = parsed("-");
    CHECK(mj_ranges_is_empty(&dash));
    word_is(&dash, "-");
    mj_ranges u = parsed("1_000-2_000,7");
    word_is(&u, "7-8,1000-2000");
    CHECK(!mj_ranges_parse("", &junk, NULL) && !mj_ranges_parse("1,", &junk, NULL) && !mj_ranges_parse("-5", &junk, NULL));
    CHECK(!mj_ranges_parse("18446744073709551616", &junk, NULL));
    for (mj_ranges *z[] = {&r, &f, &hundred, &p, &i, &e7, &e, &t, &dash, &u}, **q = z; q < z + 10; q++) mj_ranges_free(*q);
}

static uint64_t rng = 0x9e3779b97f4a7c15u;
static uint64_t rnd(uint64_t n) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng % n;
}

#define N 300
static void bits_of(const mj_ranges *r, bool *b) {
    memset(b, 0, N);
    for (size_t i = 0; i < r->n; i++) {
        CHECK(r->v[i][0] < r->v[i][1]);
        if (i) CHECK(r->v[i - 1][1] < r->v[i][0]); /* disjoint and never touching */
        for (uint64_t x = r->v[i][0]; x < r->v[i][1] && x < N; x++) b[x] = true;
    }
}

static void random_ops(void) {
    mj_ranges r = {0};
    bool ref[N] = {0}, got[N];
    for (int step = 0; step < 20000; step++) {
        uint64_t s = rnd(N), e = s + rnd(20);
        if (e > N) e = N;
        switch (rnd(5)) {
        case 0:
        case 1:
            mj_ranges_insert(&r, s, e);
            for (uint64_t x = s; x < e; x++) ref[x] = true;
            break;
        case 2: {
            bool any = false;
            for (uint64_t x = s; x < e; x++) any |= ref[x], ref[x] = false;
            CHECK(mj_ranges_remove(&r, s, e) == any);
            break;
        }
        case 3: {
            mj_ranges o = mj_ranges_of(s, e), in = mj_ranges_intersect(&r, &o);
            bits_of(&in, got);
            for (uint64_t x = 0; x < N; x++) CHECK(got[x] == (ref[x] && x >= s && x < e));
            mj_ranges_free(&o);
            mj_ranges_free(&in);
            break;
        }
        default: {
            uint64_t k = rnd(40), want = 0, n = 0, x;
            bool has = false;
            for (uint64_t y = 0; y < N; y++)
                if (ref[y] && n++ == k) {
                    want = y;
                    has = true;
                    break;
                }
            CHECK(mj_ranges_nth(&r, k, &x) == has);
            if (has) CHECK_U(x, want);
            CHECK(mj_ranges_contains(&r, s) == ref[s]);
        }
        }
        if (step % 97 == 0) {
            bits_of(&r, got);
            uint64_t n = 0;
            for (int x = 0; x < N; x++) {
                CHECK(got[x] == ref[x]);
                n += ref[x];
            }
            CHECK_U(mj_ranges_len(&r), n);
            /* The word form parses back to the same set. */
            char *w = mj_ranges_word_new(&r);
            mj_ranges back = {0};
            CHECK(mj_ranges_parse(w, &back, NULL) && mj_ranges_eq(&back, &r));
            mj_ranges_free(&back);
            free(w);
        }
    }
    mj_ranges_free(&r);
}

int main(void) {
    the_rust_test();
    random_ops();
    return mesh_check_done();
}
