/* place.rs: gather_scatter_state, composer_matches_frames. */
#include "mesh/check.h"
#include "mesh/place_compose.h"

#include <stdlib.h>

static mps_spec spec(void) {
    mps_region st[] = {{0x0, 0x8}, {0x100, 0x4}}, out[] = {{0x1000, 0x40}, {0x1400, 0x40}};
    return mps_new(2, st, 2, 0x4, out, 2, 0x2000, 4);
}

/* A fake view: byte i of region (o, n) is (o + i) ^ salt. */
static void view(void *ctx, uint32_t o, uint32_t n, uint8_t *out) {
    uint8_t salt = (uint8_t)(uintptr_t)ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t)(o + i) ^ salt;
}

typedef struct {
    uint32_t off[4], len[4];
    uint8_t b[4][16];
    int n;
} writes;

static void rec(void *ctx, uint32_t off, const uint8_t *b, uint32_t len) {
    writes *w = ctx;
    w->off[w->n] = off, w->len[w->n] = len;
    memcpy(w->b[w->n++], b, len);
}

static bool region_is(const uint8_t *b, uint32_t o, uint32_t n, uint8_t salt) {
    for (uint32_t i = 0; i < n; i++)
        if (b[i] != ((uint8_t)(o + i) ^ salt)) return false;
    return true;
}

int main(void) {
    mps_spec s = spec();
    mpw_bytes st = mpc_read_state(&s, view, (void *)0);
    CHECK_U(st.n, 12);
    writes w = {0};
    char *err = NULL;
    CHECK(mpc_write_state(&s, st.b, st.n, rec, &w, &err));
    CHECK(w.n == 2 && w.off[0] == 0 && w.len[0] == 8 && w.off[1] == 0x100 && region_is(w.b[1], 0x100, 4, 0));
    CHECK(!mpc_write_state(&s, st.b + 1, st.n - 1, rec, &w, &err));
    CHECK_S(err, "state of 11 bytes, the manifest declares 12");
    free(err);
    mpw_bytes d = mpc_gather(&s, 0xa, view, (void *)0);
    CHECK_U(d.n, 2 * mps_part_bytes(&s));
    CHECK(region_is(d.b, 0x1010, 16, 0));
    uint8_t buf[0x80] = {0};
    CHECK(mpc_scatter(&s, buf, sizeof buf, 0xa, d.b, d.n, &err));
    CHECK(region_is(buf + 0x10, 0x1010, 16, 0) && region_is(buf + 0x70, 0x1430, 16, 0));
    bool zero = true;
    for (int i = 0; i < 0x10; i++) zero &= buf[i] == 0;
    CHECK(zero);
    CHECK(!mpc_scatter(&s, buf, sizeof buf, 0xe, d.b, d.n, &err));
    CHECK_S(err, "frame data of 64 bytes, parts 0xe need 96");
    free(err);
    CHECK_U(mpc_sys_nibbles(0xa, 3), 0x3030);
    mpw_bytes_free(&st), mpw_bytes_free(&d);

    /* composer_matches_frames */
    uint64_t t = 1000;
    mpc_composer c = {0};
    mpw_bytes p0 = mpc_gather(&s, 3, view, (void *)1), p2 = mpc_gather(&s, 4, view, (void *)2), p3 = mpc_gather(&s, 8, view, (void *)3);
    mpc_whole wh;
    CHECK(mpc_add(&c, &s, 5, 3, 0, p0.b, p0.n, t, &wh, &err) == 0);
    CHECK(mpc_add(&c, &s, 6, 8, 2, p3.b, p3.n, t, &wh, &err) == 0);
    CHECK(mpc_add(&c, &s, 6, 3, 0, p0.b, p0.n, t, &wh, &err) == 0);
    CHECK(mpc_add(&c, &s, 6, 4, 1, p2.b, p2.n, t + 2000, &wh, &err) == 1);
    CHECK(wh.frame == 6 && wh.sys == 0x2100 && wh.t0 == t);
    CHECK(region_is(wh.buf, 0x1000, 16, 1) && region_is(wh.buf + 0x20, 0x1020, 16, 2) && region_is(wh.buf + 0x70, 0x1430, 16, 3));
    free(wh.buf);
    CHECK_U(c.n, 0);
    CHECK(mpc_add(&c, &s, 5, 4, 1, p2.b, p2.n, t, &wh, &err) == 0);
    CHECK(mpc_add(&c, &s, 5, 4, 1, p2.b, p2.n, t, &wh, &err) == -1);
    CHECK_S(err, "frame 5: parts 0x4 arrived twice");
    free(err);
    for (uint32_t f = 10; f < 30; f++) mpc_add(&c, &s, f, 3, 0, p0.b, p0.n, t, &wh, &err);
    CHECK_U(c.n, MPC_PENDING);
    mpc_clear(&c);
    mpw_bytes_free(&p0), mpw_bytes_free(&p2), mpw_bytes_free(&p3);
    mps_free(&s);
    return mesh_check_done();
}
