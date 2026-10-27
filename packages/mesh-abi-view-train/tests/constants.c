/* Every constant of @ajax/mesh-abi-view-train equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/view_train.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_TRAIN_T_PARAMS, UINT64_C(0));
    CHECK_U(MESH_TRAIN_T_LR, UINT64_C(16));
    CHECK_U(MESH_TRAIN_T_STEPS_PER_TASK, UINT64_C(20));
    CHECK_U(MESH_TRAIN_T_INV_BATCH, UINT64_C(24));
    CHECK_U(MESH_TRAIN_T_NEXT_STEP, UINT64_C(28));
    CHECK_U(MESH_TRAIN_T_ERR, UINT64_C(32));
    CHECK_U(MESH_TRAIN_T_PENDING, UINT64_C(36));
    CHECK_U(MESH_TRAIN_T_ROUND, UINT64_C(40));
    CHECK_U(MESH_TRAIN_T_STEP_HI, UINT64_C(44));
    CHECK_U(MESH_TRAIN_T_X, UINT64_C(256));
    CHECK_U(MESH_TRAIN_T_Y, UINT64_C(1280));
    CHECK_U(MESH_TRAIN_T_LOSS, UINT64_C(2304));
    CHECK_U(MESH_TRAIN_SAMPLES, UINT64_C(256));
    CHECK_U(MESH_TRAIN_VIEW_BYTES, UINT64_C(4096));
    CHECK_F32_BITS(MESH_TRAIN_LR, 0x3ba3d70au);
    CHECK_U(MESH_TRAIN_STEPS_PER_TASK, UINT64_C(64));
    CHECK_U(MESH_TRAIN_OP_SET_LR, UINT64_C(3));
    CHECK_U(MESH_TRAIN_C_STEPS, UINT64_C(0));
    CHECK_U(MESH_TRAIN_C_TIMEOUTS, UINT64_C(1));
    CHECK_U(MESH_TRAIN_C_INITS, UINT64_C(2));
    return mesh_check_done();
}
