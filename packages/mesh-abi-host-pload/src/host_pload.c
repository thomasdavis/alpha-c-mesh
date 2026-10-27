/* @ajax/mesh-abi-host-pload: its tables and functions. */
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
