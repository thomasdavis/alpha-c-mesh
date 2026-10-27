/* @ajax/mesh-abi-host-members: its tables and functions. */
#include "mesh/abi/host_members.h"

const char *const MESH_MEMBER_STATES[4] = {"absent", "linked", "up", "suspect"};

const mesh_field MESH_MEMBER_FIELDS[13] = {
    {MESH_M_STATE, 4},
    {MESH_M_CC, 4},
    {MESH_M_SMS, 4},
    {MESH_M_SMS_TOTAL, 4},
    {MESH_M_BLOCKS_PER_SM, 4},
    {MESH_M_RTT_US, 4},
    {MESH_M_WAIT_US, 4},
    {MESH_M_ROUNDS, 4},
    {MESH_M_ROUND, 8},
    {MESH_M_GPU, 32},
    {MESH_M_ARCH, 16},
    {MESH_M_HOST, 32},
    {MESH_M_BACKEND, 4},
};
