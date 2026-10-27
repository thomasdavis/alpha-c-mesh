/* @ajax/mesh-abi-node: Node records (N_*): a node's SM, heartbeat, tasks and busy time.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_NODE_H
#define MESH_ABI_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* ---------------------------------------------------------------- node records */
#define MESH_PROG_NODES 0x12000
#define MESH_PROG_MAX_NODES 32
#define MESH_NODE_STRIDE 32
#define MESH_PROG_NODES_BYTES (MESH_PROG_MAX_NODES * MESH_NODE_STRIDE)
#define MESH_N_SM 0x00 /* u32 %smid */
#define MESH_N_HEARTBEAT 0x04 /* u32, +1 every loop turn */
#define MESH_N_TASKS 0x08 /* u32 tasks run */
#define MESH_N_LAST_OP 0x0c /* u32 the last task's op */
#define MESH_N_BUSY_NS 0x10 /* u64 nanoseconds spent in tasks (globaltimer) */
#define MESH_NODE_FIELDS_LEN 5
extern const mesh_field MESH_NODE_FIELDS[5];

/* Node n of slot s: `node_rec(s, n)`, written by the node's lane 0. */
static inline size_t mesh_node_rec(uint32_t slot, uint32_t node) {
    return MESH_PROG_NODES + MESH_PROG_NODES_BYTES * (size_t)slot + MESH_NODE_STRIDE * (size_t)node;
}

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_N_LAST_OP + 4 <= MESH_N_BUSY_NS, "N_LAST_OP + 4 <= N_BUSY_NS");
_Static_assert(MESH_N_BUSY_NS + 8 <= MESH_NODE_STRIDE, "N_BUSY_NS + 8 <= NODE_STRIDE");

#endif
