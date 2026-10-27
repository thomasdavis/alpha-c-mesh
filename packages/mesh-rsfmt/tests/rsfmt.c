/* Strings, durations and floats as Rust's format! writes them (expected
 * strings are what rustc 1.88's format! prints for the same values). */
#include "mesh/check.h"
#include "mesh/rsfmt.h"

#include <math.h>
#include <stdlib.h>

static mesh_buf b;
static const char *s(void) { return mesh_buf_str(&b); }

static void f32_is(float v, const char *want) {
    mesh_buf_clear(&b);
    mesh_rs_f32(&b, v);
    CHECK_S(s(), want);
}
static void f64_is(double v, const char *want) {
    mesh_buf_clear(&b);
    mesh_rs_f64(&b, v);
    CHECK_S(s(), want);
}
static void dur_is(uint64_t secs, uint32_t ns, const char *want) {
    mesh_buf_clear(&b);
    mesh_rs_duration(&b, secs, ns);
    CHECK_S(s(), want);
}

int main(void) {
    mesh_rs_debug_cstr(&b, "a\"b\\c\nd\te\x01" "f'g");
    CHECK_S(s(), "\"a\\\"b\\\\c\\nd\\te\\u{1}f'g\"");
    mesh_buf_clear(&b);
    mesh_rs_debug_str(&b, "x\0y", 3);
    CHECK_S(s(), "\"x\\0y\"");
    mesh_buf_clear(&b);
    mesh_rs_debug_cstr(&b, "caf\xc3\xa9");
    CHECK_S(s(), "\"caf\xc3\xa9\"");

    dur_is(10, 0, "10s");
    dur_is(30, 0, "30s");
    dur_is(1, 500000000, "1.5s");
    dur_is(0, 500000000, "500ms");
    dur_is(0, 2500000, "2.5ms");
    dur_is(0, 40000, "40\xc2\xb5s");
    dur_is(0, 7, "7ns");
    dur_is(0, 0, "0ns");
    dur_is(2, 1, "2.000000001s");
    mesh_buf_clear(&b);
    mesh_rs_duration_ms(&b, 120000);
    CHECK_S(s(), "120s");

    f32_is(0.48f, "0.48");
    f32_is(1.0f, "1");
    f32_is(0.1f, "0.1");
    f32_is(1e20f, "100000000000000000000");
    f32_is(1e-7f, "0.0000001");
    f32_is(-2.5f, "-2.5");
    f32_is(-0.0f, "-0");
    f32_is(16777216.0f, "16777216");
    f32_is(3.4028235e38f, "340282350000000000000000000000000000000");
    f32_is(NAN, "NaN");
    f32_is(-INFINITY, "-inf");
    f64_is(0.1 + 0.2, "0.30000000000000004");
    f64_is(1234.5, "1234.5");
    {
        /* 5e-324: "0." then 323 zeros then 5 (326 characters, as rustc prints it). */
        char want[400] = "0.";
        for (int i = 0; i < 323; i++) want[2 + i] = '0';
        want[325] = '5';
        want[326] = 0;
        f64_is(5e-324, want);
    }

    mesh_buf_clear(&b);
    mesh_rs_fixed(&b, 0.125, 2);
    CHECK_S(s(), "0.12");
    mesh_buf_clear(&b);
    mesh_rs_fixed(&b, 2087.3, 0);
    CHECK_S(s(), "2087");
    mesh_buf_clear(&b);
    mesh_rs_fixed(&b, 0.25, 1);
    mesh_buf_putc(&b, ' ');
    mesh_rs_fixed(&b, 0.35, 1);
    mesh_buf_putc(&b, ' ');
    mesh_rs_fixed(&b, 2.0005, 3);
    CHECK_S(s(), "0.2 0.3 2.001");
    mesh_buf_clear(&b);
    mesh_rs_debug_cstr(&b, "caf\xc3\xa9\x7f");
    CHECK_S(s(), "\"caf\xc3\xa9\\u{7f}\"");
    /* from_utf8_lossy, as rustc 1.88 prints String::from_utf8_lossy of the same bytes. */
    mesh_buf_clear(&b);
    mesh_rs_lossy(&b, "a\xff" "b\xe2\x82" "c\xf0\x9f\x98\x80\xed\xa0\x80\xc3", 17);
    CHECK_S(s(), "a\xef\xbf\xbd" "b\xef\xbf\xbd" "c\xf0\x9f\x98\x80\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd");
    CHECK(mesh_rs_utf8_valid("caf\xc3\xa9", 5));
    CHECK(!mesh_rs_utf8_valid("\xc3", 1));
    /* str::parse: what Rust accepts and refuses. */
    uint64_t u;
    uint32_t u32;
    int64_t i64;
    CHECK_I(mesh_rs_parse_u64("", 0, &u), MESH_RS_EMPTY);
    CHECK_I(mesh_rs_parse_u64("+", 1, &u), MESH_RS_INVALID);
    CHECK_I(mesh_rs_parse_u64("+7", 2, &u), 0);
    CHECK_U(u, 7);
    CHECK_I(mesh_rs_parse_u64("-7", 2, &u), MESH_RS_INVALID);
    CHECK_I(mesh_rs_parse_u64("18446744073709551615", 20, &u), 0);
    CHECK_I(mesh_rs_parse_u64("18446744073709551616", 20, &u), MESH_RS_OVERFLOW);
    CHECK_I(mesh_rs_parse_u64("1x", 2, &u), MESH_RS_INVALID);
    CHECK_I(mesh_rs_parse_u32("4294967296", 10, &u32), MESH_RS_OVERFLOW);
    CHECK_I(mesh_rs_parse_i64("-9223372036854775808", 20, &i64), 0);
    CHECK(i64 == INT64_MIN);
    CHECK_I(mesh_rs_parse_i64("9223372036854775808", 19, &i64), MESH_RS_OVERFLOW);
    CHECK_S(mesh_rs_parse_error(MESH_RS_OVERFLOW), "number too large to fit in target type");
    float fl;
    double db;
    CHECK_I(mesh_rs_parse_f32("1.79", 4, &fl), 0);
    CHECK_F32_BITS(fl, 0x3fe51eb8u);
    CHECK_I(mesh_rs_parse_f32(".5", 2, &fl), 0);
    CHECK_I(mesh_rs_parse_f32("5.", 2, &fl), 0);
    CHECK_I(mesh_rs_parse_f32("-1e5", 4, &fl), 0);
    CHECK_I(mesh_rs_parse_f32("Infinity", 8, &fl), 0);
    CHECK_I(mesh_rs_parse_f32("NaN", 3, &fl), 0);
    CHECK_I(mesh_rs_parse_f32("0x10", 4, &fl), MESH_RS_FLOAT_INVALID);
    CHECK_I(mesh_rs_parse_f32(" 1", 2, &fl), MESH_RS_FLOAT_INVALID);
    CHECK_I(mesh_rs_parse_f32("1e", 2, &fl), MESH_RS_FLOAT_INVALID);
    CHECK_I(mesh_rs_parse_f32(".", 1, &fl), MESH_RS_FLOAT_INVALID);
    CHECK_I(mesh_rs_parse_f64("", 0, &db), MESH_RS_FLOAT_EMPTY);
    CHECK_I(mesh_rs_parse_f64("2.5e-3", 6, &db), 0);
    CHECK(db == 2.5e-3);
    mesh_buf_free(&b);
    return mesh_check_done();
}
