/* Every constant of @ajax/mesh-abi-node equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/node.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_PROG_NODES, UINT64_C(73728));
    CHECK_U(MESH_PROG_MAX_NODES, UINT64_C(32));
    CHECK_U(MESH_NODE_STRIDE, UINT64_C(32));
    CHECK_U(MESH_PROG_NODES_BYTES, UINT64_C(1024));
    CHECK_U(MESH_N_SM, UINT64_C(0));
    CHECK_U(MESH_N_HEARTBEAT, UINT64_C(4));
    CHECK_U(MESH_N_TASKS, UINT64_C(8));
    CHECK_U(MESH_N_LAST_OP, UINT64_C(12));
    CHECK_U(MESH_N_BUSY_NS, UINT64_C(16));
    CHECK_U(MESH_NODE_FIELDS_LEN, 5);
    CHECK_U(MESH_NODE_FIELDS[0].off, UINT64_C(0));
    CHECK_U(MESH_NODE_FIELDS[0].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_FIELDS[1].off, UINT64_C(4));
    CHECK_U(MESH_NODE_FIELDS[1].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_FIELDS[2].off, UINT64_C(8));
    CHECK_U(MESH_NODE_FIELDS[2].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_FIELDS[3].off, UINT64_C(12));
    CHECK_U(MESH_NODE_FIELDS[3].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_FIELDS[4].off, UINT64_C(16));
    CHECK_U(MESH_NODE_FIELDS[4].bytes, UINT64_C(8));
    return mesh_check_done();
}
