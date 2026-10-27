/* @ajax/mesh-abi-host-fleet: its tables and functions. */
#include "mesh/abi/host_fleet.h"

const char *const MESH_ROLE_NAMES[5] = {"single", "follower", "pre-candidate", "candidate", "leader"};

const char *const MESH_GS_NAMES[3] = {"-", "running", "paused"};

const mesh_field MESH_FMEMBER_FIELDS[5] = {
    {MESH_FM_STATUS, 4},
    {MESH_FM_GROUP, 4},
    {MESH_FM_APPLIED, 8},
    {MESH_FM_HEARD_MS, 8},
    {MESH_FM_REASON, 40},
};
