/* @ajax/mesh-abi-host-pload: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/host_pload.h"

const mesh_field MESH_PLOAD_FIELDS[22] = {
    {MESH_PL_FLAGS, 4},
    {MESH_PL_LAT_N, 4},
    {MESH_PL_SHARE, 4},
    {MESH_PL_SHARE10, 4},
    {MESH_PL_SLICE_US, 4},
    {MESH_PL_GAP_US, 4},
    {MESH_PL_MAX_GAP_US, 4},
    {MESH_PL_OBS, 4},
    {MESH_PL_P50_US, 4},
    {MESH_PL_P90_US, 4},
    {MESH_PL_P99_US, 4},
    {MESH_PL_MAX_US, 4},
    {MESH_PL_TASK_US, 4},
    {MESH_PL_TASKS_S, 4},
    {MESH_PL_BUSY, 4},
    {MESH_PL_QUEUE_US, 4},
    {MESH_PL_CPU_NS, 8},
    {MESH_PL_RSS, 8},
    {MESH_PL_CPU, 4},
    {MESH_PL_CPU10, 4},
    {MESH_PL_CPU_TASK_US, 4},
    {MESH_PL_UNITS, 4},
};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_PL_QUEUE_US + 4 <= MESH_PL_HOST, "PL_QUEUE_US + 4 <= PL_HOST");
_Static_assert(MESH_PL_CPU_NS == MESH_PL_HOST, "PL_CPU_NS == PL_HOST");
_Static_assert((MESH_PL_CPU_NS % 8 == 0), "PL_CPU_NS.is_multiple_of(8)");
_Static_assert((MESH_PL_RSS % 8 == 0), "PL_RSS.is_multiple_of(8)");
_Static_assert(0x1800 <= MESH_S_PLOAD, "0x1800 <= S_PLOAD");
_Static_assert((MESH_S_PLOAD % 8 == 0), "S_PLOAD.is_multiple_of(8)");
