/* @ajax/mesh-abi-node-load: its tables and functions. */
#include "mesh/abi/node_load.h"

const mesh_field MESH_NODE_LOAD_FIELDS[6] = {
    {MESH_NL_OBS_NS, 8},
    {MESH_NL_LOST_NS, 8},
    {MESH_NL_GAPS, 4},
    {MESH_NL_MAX_GAP_NS, 4},
    {MESH_NL_WIN, 4},
    {MESH_NL_QUEUE_US, 4},
};
