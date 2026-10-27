/* @ajax/mesh-abi-pipe-table: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/pipe_table.h"

const mesh_field MESH_PIPE_FIELDS[10] = {
    {MESH_PP_IN_OFF, 4},
    {MESH_PP_IN_CAP, 4},
    {MESH_PP_IN_REC, 4},
    {MESH_PP_IN_TYPE, 4},
    {MESH_PP_FEEDS_MASK, 4},
    {MESH_PP_FED_MASK, 4},
    {MESH_PP_IN_HOLD, 4},
    {MESH_PP_FLIGHT, 4},
    {MESH_PP_TYPE_NAME, MESH_NAME_BYTES},
    {MESH_PP_FLIGHT1, 4},
};
