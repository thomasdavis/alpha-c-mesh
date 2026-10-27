/* The encoding's bytes, reads back, the short-read and UTF-8 errors with jobs.rs's text. */
#include "mesh/jobs/wire.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

int main(void) {
    msb w = {0};
    mj_w_u8(&w, 1);
    mj_w_u32(&w, 0x04030201);
    mj_w_u64(&w, 0x0807060504030201u);
    mj_w_str(&w, "p");
    mj_ranges r = mj_ranges_of(1, 5);
    mj_w_ranges(&w, &r);
    static const uint8_t want[] = {1, 1, 2, 3, 4, 1, 2, 3, 4, 5, 6, 7, 8, 1, 0, 0, 0, 'p',
                                   1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0};
    CHECK_U(w.n, sizeof want);
    CHECK(memcmp(w.p, want, sizeof want) == 0);
    mj_rd rd = mj_rd_new(w.p, w.n);
    uint8_t a;
    uint32_t b;
    uint64_t c;
    char *s = NULL;
    mj_ranges back = {0};
    CHECK(mj_rd_u8(&rd, &a) && a == 1 && mj_rd_u32(&rd, &b) && b == 0x04030201 && mj_rd_u64(&rd, &c) && c == 0x0807060504030201u);
    CHECK(mj_rd_str(&rd, &s) && strcmp(s, "p") == 0 && mj_rd_ranges(&rd, &back) && mj_ranges_eq(&back, &r) && mj_rd_done(&rd));
    free(s);
    /* Short: what was left, what was wanted, where. */
    CHECK(!mj_rd_u32(&rd, &b));
    CHECK_S(rd.err, "job frame: short (0 of 4 bytes at 38)");
    CHECK(!mj_rd_u8(&rd, &a)); /* still failed, first message kept */
    CHECK_S(rd.err, "job frame: short (0 of 4 bytes at 38)");
    mj_rd_free(&rd);
    /* Not UTF-8: an overlong, a surrogate, a truncated sequence; valid ones pass. */
    static const uint8_t bad[][8] = {{2, 0, 0, 0, 0xc0, 0xaf}, {3, 0, 0, 0, 0xed, 0xa0, 0x80}, {1, 0, 0, 0, 0xe2}};
    static const size_t badn[] = {6, 7, 5};
    for (int i = 0; i < 3; i++) {
        mj_rd x = mj_rd_new(bad[i], badn[i]);
        CHECK(!mj_rd_str(&x, &s));
        CHECK_S(x.err, "job frame: not UTF-8");
        mj_rd_free(&x);
    }
    CHECK(mj_utf8_ok((const uint8_t *)"caf\xc3\xa9 \xf0\x9f\x98\x80", 10));
    CHECK(!mj_utf8_ok((const uint8_t *)"\xf4\x90\x80\x80", 4));
    /* from_utf8_lossy: a maximal invalid prefix is one U+FFFD (values from Rust). */
    static const struct { const char *in; size_t n; const char *out; } lossy[] = {
        {"ok", 2, "ok"}, {"a\xff" "b", 3, "a\xef\xbf\xbd" "b"}, {"\xe2\x82", 2, "\xef\xbf\xbd"}, {"\xe2\x82" "x", 3, "\xef\xbf\xbd" "x"},
        {"\xed\xa0\x80", 3, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"}, {"\xf0\x9f\x98", 3, "\xef\xbf\xbd"},
        {"\xc0\xaf", 2, "\xef\xbf\xbd\xef\xbf\xbd"}, {"\xf4\x90\x80\x80", 4, "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd"},
    };
    for (size_t i = 0; i < sizeof lossy / sizeof *lossy; i++) {
        char *l = mj_utf8_lossy((const uint8_t *)lossy[i].in, lossy[i].n);
        CHECK_S(l, lossy[i].out);
        free(l);
    }
    mj_ranges_free(&r);
    mj_ranges_free(&back);
    msb_free(&w);
    return mesh_check_done();
}
