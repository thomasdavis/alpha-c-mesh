/* Rust's outputs, checked by hand against rustc 1.88: `format!("{}", x)` and `format!("{:?}", s)`. */
#include "mesh/check.h"
#include "mesh/rust_fmt.h"

#include <math.h>
#include <stdlib.h>

int main(void) {
    char b[RF_FLOAT_MAX];
    CHECK_S(rf_f32(b, 1.0f), "1");
    CHECK_S(rf_f32(b, 0.5f), "0.5");
    CHECK_S(rf_f32(b, 0.1f), "0.1");
    CHECK_S(rf_f32(b, 0.3f), "0.3");
    CHECK_S(rf_f32(b, 100.0f), "100");
    CHECK_S(rf_f32(b, 1e-7f), "0.0000001");
    CHECK_S(rf_f32(b, 1e20f), "100000000000000000000");
    CHECK_S(rf_f32(b, 16777216.0f), "16777216");
    CHECK_S(rf_f32(b, 3.4028235e38f), "340282350000000000000000000000000000000");
    CHECK_S(rf_f32(b, -2.5f), "-2.5");
    CHECK_S(rf_f32(b, 0.0f), "0");
    CHECK_S(rf_f32(b, -0.0f), "-0");
    CHECK_S(rf_f32(b, NAN), "NaN");
    CHECK_S(rf_f32(b, INFINITY), "inf");
    CHECK_S(rf_f32(b, -INFINITY), "-inf");
    CHECK_S(rf_f32(b, 1.4e-45f), "0.000000000000000000000000000000000000000000001");
    CHECK_S(rf_f32(b, 0.25f * 1.33f), "0.3325");
    CHECK_S(rf_f32(b, 123456.789f), "123456.79");
    CHECK_S(rf_f64(b, 0.1), "0.1");
    CHECK_S(rf_f64(b, 0.1 + 0.2), "0.30000000000000004");
    CHECK_S(rf_f64(b, 1e21), "1000000000000000000000");
    CHECK_S(rf_f64(b, 2.5), "2.5");
    CHECK_S(rf_f64(b, 1.0 / 3.0), "0.3333333333333333");
    CHECK_S(rf_f64(b, (double)0.1f), "0.10000000149011612");
    char *d = rf_debug_str("a\"b\\c\nd\te\rf");
    CHECK_S(d, "\"a\\\"b\\\\c\\nd\\te\\rf\"");
    free(d);
    d = rf_debug_str("x\x01y'");
    CHECK_S(d, "\"x\\u{1}y'\"");
    free(d);
    d = rf_debug_str("");
    CHECK_S(d, "\"\"");
    free(d);
    char h[24];
    CHECK_S(rf_hex_alt(h, 0), "0x0");
    CHECK_S(rf_hex_alt(h, 0xbeef), "0xbeef");
    /* Every f32 Display reads back as the same f32 (a sweep over bit patterns). */
    for (uint32_t u = 1; u < 0x7f800000u; u += 0x00012345u) {
        float f;
        memcpy(&f, &u, 4);
        rf_f32(b, f);
        CHECK(strtof(b, NULL) == f);
    }
    return mesh_check_done();
}
