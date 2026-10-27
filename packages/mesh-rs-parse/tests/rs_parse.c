/* mesh-rs-parse against what Rust's parse::<T>() and from_str_radix accept and refuse. */
#include "mesh/rs_parse.h"
#include "mesh/check.h"

#include <string.h>

static bool u64s(const char *s, uint64_t *v) { return rs_u64(s, strlen(s), v); }

int main(void) {
    uint64_t v;
    CHECK(u64s("0", &v) && v == 0);
    CHECK(u64s("+7", &v) && v == 7);
    CHECK(u64s("18446744073709551615", &v) && v == UINT64_MAX);
    CHECK(!u64s("18446744073709551616", &v));
    CHECK(!u64s("", &v) && !u64s("+", &v) && !u64s("-0", &v) && !u64s(" 1", &v) && !u64s("1 ", &v) && !u64s("0x1", &v));
    uint32_t u;
    CHECK(rs_u32("4294967295", 10, &u) && u == UINT32_MAX);
    CHECK(!rs_u32("4294967296", 10, &u));
    int32_t i;
    CHECK(rs_i32("-2147483648", 11, &i) && i == INT32_MIN);
    CHECK(rs_i32("2147483647", 10, &i) && i == INT32_MAX);
    CHECK(rs_i32("-0", 2, &i) && i == 0);
    CHECK(rs_i32("+5", 2, &i) && i == 5);
    CHECK(!rs_i32("2147483648", 10, &i) && !rs_i32("-2147483649", 11, &i) && !rs_i32("-", 1, &i));
    CHECK(rs_parse_uint("fF", 2, 16, UINT32_MAX, &v) && v == 255);
    CHECK(rs_parse_uint("+a", 2, 16, UINT32_MAX, &v) && v == 10);
    CHECK(!rs_parse_uint("g", 1, 16, UINT32_MAX, &v) && !rs_parse_uint("100000000", 9, 16, UINT32_MAX, &v));
    int64_t w;
    CHECK(rs_parse_int("-9223372036854775808", 20, INT64_MIN, INT64_MAX, &w) && w == INT64_MIN);
    return mesh_check_done();
}
