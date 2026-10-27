/* Every constant of @ajax/mesh-abi-node-load equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/node_load.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_NODE_LOAD, UINT64_C(262144));
    CHECK_U(MESH_NL_STRIDE, UINT64_C(32));
    CHECK_U(MESH_NL_OBS_NS, UINT64_C(0));
    CHECK_U(MESH_NL_LOST_NS, UINT64_C(8));
    CHECK_U(MESH_NL_GAPS, UINT64_C(16));
    CHECK_U(MESH_NL_MAX_GAP_NS, UINT64_C(20));
    CHECK_U(MESH_NL_WIN, UINT64_C(24));
    CHECK_U(MESH_NL_QUEUE_US, UINT64_C(28));
    CHECK_U(MESH_GAP_NS, UINT64_C(20000));
    CHECK_U(MESH_NODE_LOAD_FIELDS_LEN, 6);
    CHECK_U(MESH_NODE_LOAD_FIELDS[0].off, UINT64_C(0));
    CHECK_U(MESH_NODE_LOAD_FIELDS[0].bytes, UINT64_C(8));
    CHECK_U(MESH_NODE_LOAD_FIELDS[1].off, UINT64_C(8));
    CHECK_U(MESH_NODE_LOAD_FIELDS[1].bytes, UINT64_C(8));
    CHECK_U(MESH_NODE_LOAD_FIELDS[2].off, UINT64_C(16));
    CHECK_U(MESH_NODE_LOAD_FIELDS[2].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_LOAD_FIELDS[3].off, UINT64_C(20));
    CHECK_U(MESH_NODE_LOAD_FIELDS[3].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_LOAD_FIELDS[4].off, UINT64_C(24));
    CHECK_U(MESH_NODE_LOAD_FIELDS[4].bytes, UINT64_C(4));
    CHECK_U(MESH_NODE_LOAD_FIELDS[5].off, UINT64_C(28));
    CHECK_U(MESH_NODE_LOAD_FIELDS[5].bytes, UINT64_C(4));
    return mesh_check_done();
}
