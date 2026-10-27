/* Every constant of @ajax/mesh-abi-place-ops equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/place_ops.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_PL_REQ, UINT64_C(0));
    CHECK_U(MESH_PL_RESULT, UINT64_C(1));
    CHECK_U(MESH_PL_PAUSE, UINT64_C(2));
    CHECK_U(MESH_PL_PAUSED, UINT64_C(3));
    CHECK_U(MESH_PL_SET, UINT64_C(4));
    CHECK_U(MESH_PL_DONE, UINT64_C(5));
    CHECK_U(MESH_PL_FAIL, UINT64_C(6));
    return mesh_check_done();
}
