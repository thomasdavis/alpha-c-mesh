/* @ajax/mesh-abi-program-table: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/program_table.h"

const char *const MESH_STATES[5] = {"empty", "loaded", "running", "stopping", "failed"};

const char *const MESH_POLICIES[4] = {"continuous", "periodic", "on_demand", "job"};

const char *const MESH_PEER_MODES[2] = {"none", "lockstep"};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_POLICIES_LEN == MESH_POLICY_JOB  + 1, "POLICIES.len() == POLICY_JOB as usize + 1");
_Static_assert(MESH_PT_LOADS + 4 <= MESH_PT_BYTES, "PT_LOADS + 4 <= PT_BYTES");
_Static_assert((MESH_PT_PTX_HASH % 8 == 0), "PT_PTX_HASH.is_multiple_of(8)");
