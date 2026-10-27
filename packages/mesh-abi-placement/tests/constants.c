/* Every constant of @ajax/mesh-abi-placement equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/placement.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_PLACE_CAP_NONE, UINT64_C(0));
    CHECK_U(MESH_PLACE_CAP_PIN, UINT64_C(1));
    CHECK_U(MESH_PLACE_CAP_SPLIT, UINT64_C(2));
    CHECK_U(MESH_PLACE_CAPS_LEN, 3);
    CHECK_S(MESH_PLACE_CAPS[0], "none");
    CHECK_S(MESH_PLACE_CAPS[1], "pin");
    CHECK_S(MESH_PLACE_CAPS[2], "split");
    CHECK_U(MESH_PLACE_LOCAL, UINT64_C(0));
    CHECK_U(MESH_PLACE_PIN, UINT64_C(1));
    CHECK_U(MESH_PLACE_SPLIT, UINT64_C(2));
    CHECK_U(MESH_PLACES_LEN, 3);
    CHECK_S(MESH_PLACES[0], "local");
    CHECK_S(MESH_PLACES[1], "pin");
    CHECK_S(MESH_PLACES[2], "split");
    CHECK_U(MESH_PLACE_PARTS_MAX, UINT64_C(32));
    CHECK_U(MESH_PART_NONE, UINT64_C(255));
    CHECK_U(MESH_SH_SEQ, UINT64_C(0));
    CHECK_U(MESH_SH_MODE, UINT64_C(4));
    CHECK_U(MESH_SH_SYS, UINT64_C(8));
    CHECK_U(MESH_SH_LAT_US, UINT64_C(12));
    CHECK_U(MESH_SH_BUF, UINT64_C(16));
    CHECK_U(MESH_SH_BUF_BYTES, UINT64_C(16));
    CHECK_U(MESH_SHB_FRAME, UINT64_C(0));
    CHECK_U(MESH_SHB_SYS, UINT64_C(4));
    CHECK_U(MESH_SH_DATA, UINT64_C(256));
    return mesh_check_done();
}
