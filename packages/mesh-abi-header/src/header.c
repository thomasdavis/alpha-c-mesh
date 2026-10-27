/* @ajax/mesh-abi-header: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/header.h"

const mesh_field MESH_HEADER_FIELDS[19] = {
    {MESH_H_MAGIC, 4},
    {MESH_H_VERSION, 4},
    {MESH_H_PEER, 4},
    {MESH_H_BLINK, 4},
    {MESH_H_EPOCH, 4},
    {MESH_H_PROG_SEQ, 4},
    {MESH_H_SMS, 4},
    {MESH_H_SYSTEMS, 4},
    {MESH_H_CC, 4},
    {MESH_H_BLOCKS_PER_SM, 4},
    {MESH_H_SMS_TOTAL, 4},
    {MESH_H_BACKEND, 4},
    {MESH_H_MAX_TG_THREADS, 4},
    {MESH_H_LOAD_WIN, 4},
    {MESH_H_GROUP_RANK, 4},
    {MESH_H_GROUP_N, 4},
    {MESH_H_GROUP_EPOCH, 4},
    {MESH_H_PIPE_EPOCH, 4},
    {MESH_H_MAX_SHARED, 4},
};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_H_SMS < MESH_H_SYSTEMS, "H_SMS < H_SYSTEMS");
_Static_assert(MESH_H_SYSTEMS < MESH_H_CC, "H_SYSTEMS < H_CC");
_Static_assert(MESH_H_SMS_TOTAL + 4 <= MESH_H_BACKEND, "H_SMS_TOTAL + 4 <= H_BACKEND");
_Static_assert(MESH_H_MAX_TG_THREADS + 4 <= MESH_H_BYTES, "H_MAX_TG_THREADS + 4 <= H_BYTES");
_Static_assert(MESH_H_SYSTEMS + 4 <= MESH_H_BYTES, "H_SYSTEMS + 4 <= H_BYTES");
