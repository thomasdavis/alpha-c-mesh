/* @ajax/mesh-abi-node: its tables and functions. */
#include "mesh/abi/node.h"

const mesh_field MESH_NODE_FIELDS[5] = {
    {MESH_N_SM, 4},
    {MESH_N_HEARTBEAT, 4},
    {MESH_N_TASKS, 4},
    {MESH_N_LAST_OP, 4},
    {MESH_N_BUSY_NS, 8},
};
