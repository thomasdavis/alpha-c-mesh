/* Duration's float conversions as Rust computes them, and the virtual clock. */
#include "mesh/check.h"
#include "mesh/top_clock.h"

int main(void) {
    CHECK_F32_BITS(mt_secs_f32(MT_MS(66)), 0x3d872b02u); /* 0.066f */
    CHECK(mt_secs_f64(MT_SECS(20) + 5) == 20.000000005);
    CHECK_U(mt_since(5, 9), 0);
    CHECK_U(mt_since(9, 5), 4);
    mt_clock_virtual(true, 1000);
    CHECK_U(mt_now(), 1000);
    mt_clock_virtual(false, 0);
    mt_instant a = mt_now(), b = mt_now();
    CHECK(b >= a);
    CHECK_U(mt_from_secs_f32(10.0f), MT_SECS(10));
    return mesh_check_done();
}
