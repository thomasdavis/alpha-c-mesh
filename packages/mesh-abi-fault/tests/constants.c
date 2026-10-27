/* Every constant of @ajax/mesh-abi-fault equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/fault.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_FAULT_VIEW_BOUNDS, UINT64_C(1));
    CHECK_U(MESH_FAULT_PEER_VIEW, UINT64_C(2));
    CHECK_U(MESH_FAULT_RESP_BOUNDS, UINT64_C(3));
    CHECK_U(MESH_FAULT_GPU, UINT64_C(4));
    CHECK_U(MESH_FAULT_TRAP, UINT64_C(5));
    CHECK_U(MESH_FAULT_TIME, UINT64_C(6));
    CHECK_U(MESH_FAULT_PIPE, UINT64_C(7));
    CHECK_U(MESH_FAULT_SHARED_BOUNDS, UINT64_C(8));
    return mesh_check_done();
}
