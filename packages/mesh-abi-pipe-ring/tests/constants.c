/* Every constant of @ajax/mesh-abi-pipe-ring equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/pipe_ring.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_PIPE_PI_HEAD, UINT64_C(0));
    CHECK_U(MESH_PIPE_PI_FULL, UINT64_C(4));
    CHECK_U(MESH_PIPE_PI_TAIL, UINT64_C(64));
    CHECK_U(MESH_PIPE_PI_EMPTY, UINT64_C(68));
    CHECK_U(MESH_PIPE_PI_LAPS, UINT64_C(128));
    CHECK_U(MESH_PIPE_REC_MAX, UINT64_C(4096));
    CHECK_U(MESH_PIPE_CAP_MAX, UINT64_C(65536));
    CHECK_U(MESH_PIPE_SIDE_CPU, UINT64_C(0));
    CHECK_U(MESH_PIPE_SIDE_GPU, UINT64_C(1));
    CHECK_U(MESH_PIPE_TRIES, UINT64_C(256));
    CHECK_U(MESH_PIPE_OWNER_TAG, UINT64_C(2119671808));
    return mesh_check_done();
}
