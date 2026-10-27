/* @ajax/mesh-abi-ext: its tables and functions. */
#include "mesh/abi/ext.h"

const char *const MESH_FIELD_TYPES[5] = {"u32", "i32", "f32", "u64", "u8"};

const mesh_field MESH_EXT_FIELDS[15] = {
    {MESH_PX_COUNTER_NAMES, MESH_NAME_BYTES * MESH_COUNTERS},
    {MESH_PX_FIELD_COUNT, 4},
    {MESH_PX_PLACE_CAP, 4},
    {MESH_PX_PLACE, 4},
    {MESH_PX_PLACE_SYS, 4},
    {MESH_PX_PLACE_ASSIGN, MESH_PLACE_PARTS_MAX},
    {MESH_PX_PER_SM, 4},
    {MESH_PX_LOCAL, 4},
    {MESH_PX_PARAM, 4},
    {MESH_PX_EXEC, 4},
    {MESH_PX_FIELDS, MESH_FIELD_BYTES * MESH_MAX_FIELDS},
    {MESH_PX_SHARED, 4},
    {MESH_PX_REG_CAP, 4},
    {MESH_PX_SCRATCH_BYTES, 4},
    {MESH_PX_SCRATCH, 8},
};
