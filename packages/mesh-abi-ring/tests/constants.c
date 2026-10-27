/* Every constant of @ajax/mesh-abi-ring equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/ring.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_P_RING, UINT64_C(512));
    CHECK_U(MESH_PROG_RING, UINT64_C(64));
    CHECK_U(MESH_ENTRY_BYTES, UINT64_C(16));
    CHECK_U(MESH_E_OP, UINT64_C(0));
    CHECK_U(MESH_E_ARG, UINT64_C(4));
    CHECK_U(MESH_E_SLOT, UINT64_C(8));
    CHECK_U(MESH_E_SEQ, UINT64_C(12));
    CHECK_U(MESH_P_RESULTS, UINT64_C(1536));
    CHECK_U(MESH_P_MARKS, UINT64_C(1792));
    CHECK_U(MESH_PROG_RESULTS, UINT64_C(64));
    return mesh_check_done();
}
