/* @ajax/mesh-abi-node: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/node.h"

const mesh_field MESH_NODE_FIELDS[5] = {
    {MESH_N_SM, 4},
    {MESH_N_HEARTBEAT, 4},
    {MESH_N_TASKS, 4},
    {MESH_N_LAST_OP, 4},
    {MESH_N_BUSY_NS, 8},
};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_N_LAST_OP + 4 <= MESH_N_BUSY_NS, "N_LAST_OP + 4 <= N_BUSY_NS");
_Static_assert(MESH_N_BUSY_NS + 8 <= MESH_NODE_STRIDE, "N_BUSY_NS + 8 <= NODE_STRIDE");
