/* Every constant of @ajax/mesh-abi-view-infer equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/view_infer.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_INFER_I_PARAMS, UINT64_C(0));
    CHECK_U(MESH_INFER_I_RUNS, UINT64_C(16));
    CHECK_U(MESH_INFER_I_TRAIN_STEP, UINT64_C(20));
    CHECK_U(MESH_INFER_I_X, UINT64_C(256));
    CHECK_U(MESH_INFER_I_Y, UINT64_C(768));
    CHECK_U(MESH_INFER_POINTS, UINT64_C(64));
    CHECK_U(MESH_INFER_VIEW_BYTES, UINT64_C(4096));
    CHECK_U(MESH_INFER_OP_PROBE_READ, UINT64_C(3));
    CHECK_U(MESH_INFER_R_NO_VIEW, UINT64_C(4294967294));
    CHECK_U(MESH_INFER_R_NO_TRAIN, UINT64_C(0));
    return mesh_check_done();
}
