/* Every constant of @ajax/mesh-abi-snapshot equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/snapshot.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_SNAP_VERSION, UINT64_C(3));
    CHECK_U(MESH_SN_TOTAL, UINT64_C(0));
    CHECK_U(MESH_SN_COUNT, UINT64_C(4));
    CHECK_U(MESH_SN_VERSION, UINT64_C(8));
    CHECK_U(MESH_SN_HOST_AT, UINT64_C(12));
    CHECK_U(MESH_SN_HEAD_BYTES, UINT64_C(16));
    CHECK_U(MESH_SC_SLOT, UINT64_C(0));
    CHECK_U(MESH_SC_BYTES, UINT64_C(4));
    CHECK_U(MESH_SC_NODES, UINT64_C(8));
    CHECK_U(MESH_SC_VIEW, UINT64_C(12));
    CHECK_U(MESH_SC_HEAD_BYTES, UINT64_C(16));
    CHECK_U(MESH_SC_CTL_BYTES, UINT64_C(512));
    CHECK_U(MESH_SC_ENTRY, UINT64_C(16));
    CHECK_U(MESH_SC_CTL, UINT64_C(144));
    CHECK_U(MESH_SC_EXT, UINT64_C(656));
    CHECK_U(MESH_SC_NODE_RECS, UINT64_C(1680));
    CHECK_U(MESH_SNAP_HOST, UINT64_C(12288));
    return mesh_check_done();
}
