/* @ajax/mesh-abi-program-table: its tables and functions. */
#include "mesh/abi/program_table.h"

const char *const MESH_STATES[5] = {"empty", "loaded", "running", "stopping", "failed"};

const char *const MESH_POLICIES[4] = {"continuous", "periodic", "on_demand", "job"};

const char *const MESH_PEER_MODES[2] = {"none", "lockstep"};
