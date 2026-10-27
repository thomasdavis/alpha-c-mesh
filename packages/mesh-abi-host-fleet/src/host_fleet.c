/* @ajax/mesh-abi-host-fleet: its tables and functions, and its compile-time layout checks. */
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

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_SG_AUTO_MS + 4 <= MESH_S_FLEET_END, "SG_AUTO_MS + 4 <= S_FLEET_END");
_Static_assert((MESH_S_TERM % 8 == 0), "S_TERM.is_multiple_of(8)");
_Static_assert((MESH_S_STATE_VER % 8 == 0), "S_STATE_VER.is_multiple_of(8)");
_Static_assert((MESH_S_APPLIED_VER % 8 == 0), "S_APPLIED_VER.is_multiple_of(8)");
_Static_assert((MESH_FM_APPLIED % 8 == 0), "FM_APPLIED.is_multiple_of(8)");
_Static_assert((MESH_FMEMBER_BYTES % 8 == 0), "FMEMBER_BYTES.is_multiple_of(8)");
_Static_assert((MESH_S_FMEMBERS % 8 == 0), "S_FMEMBERS.is_multiple_of(8)");
