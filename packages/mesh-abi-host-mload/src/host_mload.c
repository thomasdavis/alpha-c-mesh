/* @ajax/mesh-abi-host-mload: its tables and functions. */
#include "mesh/abi/host_mload.h"

const mesh_field MESH_MLOAD_FIELDS[15] = {
    {MESH_ML_AT_MS, 8},
    {MESH_ML_SHARE, 4},
    {MESH_ML_SHARE10, 4},
    {MESH_ML_MAX_GAP_US, 4},
    {MESH_ML_CPU, 4},
    {MESH_ML_CPU_BIG, 4},
    {MESH_ML_CPU_LITTLE, 4},
    {MESH_ML_PSI_CPU, 4},
    {MESH_ML_PSI_MEM, 4},
    {MESH_ML_MESHD_CPU, 4},
    {MESH_ML_MEM_AVAIL_MB, 4},
    {MESH_ML_MEM_TOTAL_MB, 4},
    {MESH_ML_DISK_FREE_MB, 4},
    {MESH_ML_CPUS, 4},
    {MESH_ML_FLAGS, 4},
};
