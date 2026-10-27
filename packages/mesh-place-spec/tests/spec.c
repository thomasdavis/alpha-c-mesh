/* The spec of place.rs's tests: parts of each output region, in the view and in a show buffer. */
#include "mesh/check.h"
#include "mesh/place_spec.h"

int main(void) {
    mps_region st[] = {{0x0, 0x8}, {0x100, 0x4}}, out[] = {{0x1000, 0x40}, {0x1400, 0x40}};
    mps_spec s = mps_new(2, st, 2, 0x4, out, 2, 0x2000, 4);
    CHECK_U(mps_state_bytes(&s), 12);
    CHECK_U(mps_out_bytes(&s), 0x80);
    CHECK_U(mps_part_bytes(&s), 0x20);
    mps_region p[2];
    mps_part(&s, 3, p);
    CHECK(p[0].off == 0x1030 && p[0].len == 0x10 && p[1].off == 0x1430 && p[1].len == 0x10);
    mps_part_in_buffer(&s, 3, p);
    CHECK(p[0].off == 0x30 && p[1].off == 0x70 && p[1].len == 0x10);
    mps_spec c = mps_clone(&s);
    CHECK(c.nout == 2 && c.out[1].off == 0x1400 && c.fanout == 4);
    mps_free(&c);
    mps_free(&s);
    return mesh_check_done();
}
