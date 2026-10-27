/* @ajax/mesh-abi-node-load: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/node_load.h"

const mesh_field MESH_NODE_LOAD_FIELDS[6] = {
    {MESH_NL_OBS_NS, 8},
    {MESH_NL_LOST_NS, 8},
    {MESH_NL_GAPS, 4},
    {MESH_NL_MAX_GAP_NS, 4},
    {MESH_NL_WIN, 4},
    {MESH_NL_QUEUE_US, 4},
};

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_NL_OBS_NS % 8 == 0), "NL_OBS_NS.is_multiple_of(8)");
_Static_assert((MESH_NL_LOST_NS % 8 == 0), "NL_LOST_NS.is_multiple_of(8)");
