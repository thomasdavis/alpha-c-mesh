/* @ajax/mesh-abi-host-mload: its tables and functions, and its compile-time layout checks. */
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

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_ML_SHARE + MESH_LOADREP_BYTES == MESH_MLOAD_BYTES, "ML_SHARE + LOADREP_BYTES == MLOAD_BYTES");
_Static_assert((MESH_S_MLOAD % 8 == 0), "S_MLOAD.is_multiple_of(8)");
_Static_assert((MESH_MLOAD_BYTES % 8 == 0), "MLOAD_BYTES.is_multiple_of(8)");
