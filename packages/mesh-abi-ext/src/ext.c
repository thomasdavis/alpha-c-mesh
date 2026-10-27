/* @ajax/mesh-abi-ext: its tables and functions, and its compile-time layout checks. */
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

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_PX_LOCAL + 4 <= MESH_PX_FIELDS, "PX_LOCAL + 4 <= PX_FIELDS");
_Static_assert(MESH_PX_FIELDS + MESH_FIELD_BYTES * MESH_MAX_FIELDS <= MESH_PX_SHARED, "PX_FIELDS + FIELD_BYTES * MAX_FIELDS <= PX_SHARED");
_Static_assert(MESH_PX_SCRATCH + 8 <= MESH_PX_BYTES, "PX_SCRATCH + 8 <= PX_BYTES");
_Static_assert((MESH_PX_SCRATCH % 8 == 0), "PX_SCRATCH.is_multiple_of(8)");
_Static_assert(MESH_PX_FIELD_COUNT < MESH_PX_PLACE_CAP, "PX_FIELD_COUNT < PX_PLACE_CAP");
_Static_assert(MESH_PX_PLACE_SYS + 4 <= MESH_PX_PLACE_ASSIGN, "PX_PLACE_SYS + 4 <= PX_PLACE_ASSIGN");
_Static_assert(MESH_FD_OFF + 4 <= MESH_FIELD_BYTES, "FD_OFF + 4 <= FIELD_BYTES");
_Static_assert(MESH_FD_NAME + MESH_FD_NAME_MAX < MESH_FD_TYPE, "FD_NAME + FD_NAME_MAX < FD_TYPE");
