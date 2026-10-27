/* @ajax/mesh-abi-host-load: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/host_load.h"

const char *const MESH_THERMAL_STATES[4] = {"nominal", "fair", "serious", "critical"};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_S_LOAD_AT_MS == MESH_S_LOAD, "S_LOAD_AT_MS == S_LOAD");
_Static_assert(MESH_S_SAMPLE_US + 4 <= MESH_S_EXEC_CPU, "S_SAMPLE_US + 4 <= S_EXEC_CPU");
_Static_assert(MESH_S_LOG_DROPPED + 4 <= MESH_S_CORE_UTIL, "S_LOG_DROPPED + 4 <= S_CORE_UTIL");
_Static_assert((MESH_S_EXEC_RSS % 8 == 0), "S_EXEC_RSS.is_multiple_of(8)");
_Static_assert(MESH_S_CORE_UTIL + MESH_MAX_CPUS <= MESH_S_CORE_CLASS, "S_CORE_UTIL + MAX_CPUS <= S_CORE_CLASS");
_Static_assert(MESH_LOAD_WINDOW >= 2, "LOAD_WINDOW >= 2");
