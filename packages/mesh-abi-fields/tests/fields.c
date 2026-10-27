/* disjoint and fields_end, with the cases of the Rust's layout_tests
 * (the clash the n-way / being-port merge had: H_SYSTEMS and H_CC both at 0x30). */
#include "mesh/abi/fields.h"
#include "mesh/check.h"

int main(void) {
    const mesh_field same[] = {{0x30, 4}, {0x30, 4}}, inside[] = {{0x50, 32}, {0x54, 4}}, apart[] = {{0x30, 4}, {0x34, 4}};
    CHECK(!mesh_disjoint(same, 2));
    CHECK(!mesh_disjoint(inside, 2));
    CHECK(mesh_disjoint(apart, 2));
    CHECK(mesh_disjoint(apart, 0));
    CHECK_U(mesh_fields_end(apart, 2), 0x38);
    CHECK_U(mesh_fields_end(inside, 2), 0x70);
    CHECK_U(mesh_fields_end(apart, 0), 0);
    return mesh_check_done();
}
