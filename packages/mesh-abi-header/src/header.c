/* @ajax/mesh-abi-header: its tables and functions. */
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
