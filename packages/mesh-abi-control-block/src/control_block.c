/* @ajax/mesh-abi-control-block: its tables and functions. */
#include "mesh/abi/control_block.h"

const mesh_field MESH_CTL_FIELDS[17] = {
    {MESH_P_SHUTDOWN, 4},
    {MESH_P_ONLINE, 4},
    {MESH_P_HALTED, 4},
    {MESH_P_RUN, 4},
    {MESH_P_RUN_REQ, 4},
    {MESH_P_FAULT, 4},
    {MESH_P_RING_TAIL, 8},
    {MESH_P_REQ_SEQ, 8},
    {MESH_P_RESP_SEQ, 8},
    {MESH_P_REQ_LEN, 4},
    {MESH_P_RESP_LEN, 4},
    {MESH_P_LAUNCHES, 4},
    {MESH_P_PID, 4},
    {MESH_P_SPAWNS, 4},
    {MESH_P_CRASHES, 4},
    {MESH_P_EXIT, 4},
    {MESH_P_LIMITS, 4},
};
