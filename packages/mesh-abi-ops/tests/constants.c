/* Every constant of @ajax/mesh-abi-ops equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/ops.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_OP_TASK, UINT64_C(0));
    CHECK_U(MESH_OP_INIT, UINT64_C(1));
    CHECK_U(MESH_OP_REQ, UINT64_C(2));
    CHECK_U(MESH_OP_FOLLOW, UINT64_C(255));
    CHECK_U(MESH_OP_JOB, UINT64_C(254));
    CHECK_U(MESH_R_JOB_FAIL, UINT64_C(4294967295));
    CHECK_U(MESH_R_NONE, UINT64_C(0));
    CHECK_U(MESH_R_BAD_OP, UINT64_C(4294967295));
    return mesh_check_done();
}
