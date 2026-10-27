/* Every constant of @ajax/mesh-abi-reconcile equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/reconcile.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_RS_NONE, UINT64_C(0));
    CHECK_U(MESH_RS_SYNC, UINT64_C(1));
    CHECK_U(MESH_RS_PENDING, UINT64_C(2));
    CHECK_U(MESH_RS_FAILED, UINT64_C(3));
    CHECK_U(MESH_RS_NAMES_LEN, 4);
    CHECK_S(MESH_RS_NAMES[0], "-");
    CHECK_S(MESH_RS_NAMES[1], "in-sync");
    CHECK_S(MESH_RS_NAMES[2], "pending");
    CHECK_S(MESH_RS_NAMES[3], "failed");
    return mesh_check_done();
}
