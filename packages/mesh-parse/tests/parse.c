/* What Rust's parse accepts and refuses. */
#include "mesh/check.h"
#include "mesh/parse.h"

#include <math.h>

int main(void) {
    uint64_t u;
    uint32_t w;
    int32_t i;
    float f;
    double d;
    CHECK(mp_u64("0", &u) && u == 0);
    CHECK(mp_u64("+42", &u) && u == 42);
    CHECK(mp_u64("18446744073709551615", &u) && u == UINT64_MAX);
    CHECK(!mp_u64("18446744073709551616", &u) && !mp_u64("", &u) && !mp_u64("+", &u) && !mp_u64("-1", &u));
    CHECK(!mp_u64(" 1", &u) && !mp_u64("1 ", &u) && !mp_u64("0x10", &u) && !mp_u64("1_000", &u));
    CHECK(mp_u32("4294967295", &w) && w == UINT32_MAX && !mp_u32("4294967296", &w));
    uint16_t h;
    CHECK(mp_u16("27801", &h) && h == 27801 && !mp_u16("65536", &h) && !mp_u16("x", &h));
    CHECK(mp_i32("-2", &i) && i == -2 && mp_i32("+7", &i) && i == 7);
    CHECK(mp_i32("-2147483648", &i) && i == INT32_MIN && !mp_i32("2147483648", &i) && !mp_i32("-+1", &i) && !mp_i32("-", &i));
    CHECK(mp_hex_u64("00000000000000aB", &u) && u == 0xab && mp_hex_u64("ffffffffffffffff", &u) && u == UINT64_MAX);
    CHECK(!mp_hex_u64("0x1", &u) && !mp_hex_u64("g", &u) && !mp_hex_u64("10000000000000000", &u));
    CHECK(mp_hex_u32("ff", &w) && w == 255);
    CHECK(mp_f32("0.5", &f) && f == 0.5f && mp_f32(".5", &f) && mp_f32("5.", &f) && f == 5.0f && mp_f32("1e3", &f) && f == 1000.0f);
    CHECK(mp_f32("-2.5E-1", &f) && f == -0.25f && mp_f32("+3", &f));
    CHECK(mp_f32("nan", &f) && isnan(f) && mp_f32("NaN", &f) && mp_f32("inf", &f) && isinf(f) && mp_f32("-Infinity", &f) && f < 0);
    CHECK(!mp_f32("", &f) && !mp_f32(".", &f) && !mp_f32("1e", &f) && !mp_f32(" 1", &f) && !mp_f32("0x1p3", &f) && !mp_f32("1.5x", &f));
    CHECK(mp_f64("0.25", &d) && d == 0.25 && !mp_f64("abc", &d));
    return mesh_check_done();
}
