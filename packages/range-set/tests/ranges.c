/* Merging, splitting, saturating lengths; and a random cross-check against a bitmap. */
#include "mesh/check.h"
#include "mesh/range_set.h"

int main(void) {
    rs_set r = {0};
    rs_insert(&r, 10, 20);
    rs_insert(&r, 30, 40);
    rs_insert(&r, 20, 25); /* touches: merges */
    CHECK(r.n == 2 && r.s[0] == 10 && r.e[0] == 25);
    rs_insert(&r, 0, 100);
    CHECK(r.n == 1 && rs_len(&r) == 100);
    CHECK(rs_remove(&r, 40, 50) && r.n == 2 && rs_contains(&r, 39) && !rs_contains(&r, 40) && rs_contains(&r, 50));
    CHECK(!rs_remove(&r, 40, 50) && !rs_remove(&r, 5, 5));
    CHECK(rs_take(&r, 0) && !rs_contains(&r, 0));
    uint64_t x;
    CHECK(rs_first(&r, &x) && x == 1 && rs_end(&r, &x) && x == 100);
    rs_set big = rs_of(0, UINT64_MAX);
    rs_insert(&big, 5, 6);
    CHECK(rs_len(&big) == UINT64_MAX);
    rs_free(&big);
    /* random operations against a bitmap */
    uint64_t rng = 7;
    for (int round = 0; round < 200; round++) {
        rs_set s = {0};
        uint8_t bits[256] = {0};
        for (int op = 0; op < 60; op++) {
            rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
            uint64_t a = rng % 256, b = a + (rng >> 20) % 40;
            if (b > 256) b = 256;
            if ((rng >> 40) % 3) {
                rs_insert(&s, a, b);
                for (uint64_t i = a; i < b; i++) bits[i] = 1;
            } else {
                bool had = false;
                for (uint64_t i = a; i < b; i++) had |= bits[i], bits[i] = 0;
                CHECK(rs_remove(&s, a, b) == had);
            }
        }
        uint64_t n = 0;
        for (int i = 0; i < 256; i++) {
            CHECK(rs_contains(&s, (uint64_t)i) == (bits[i] == 1));
            n += bits[i];
        }
        CHECK_U(rs_len(&s), n);
        for (size_t i = 1; i < s.n; i++) CHECK(s.e[i - 1] < s.s[i]);
        rs_set c = rs_clone(&s);
        CHECK(rs_eq(&c, &s));
        rs_subtract(&c, &s);
        CHECK(rs_is_empty(&c));
        rs_union(&c, &s);
        CHECK(rs_eq(&c, &s));
        rs_free(&c);
        rs_free(&s);
    }
    rs_free(&r);
    return mesh_check_done();
}
