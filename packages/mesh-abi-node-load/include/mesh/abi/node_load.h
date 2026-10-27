/* @ajax/mesh-abi-node-load: Node load records (NL_*): how much of the GPU the nodes got, measured from inside.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_NODE_LOAD_H
#define MESH_ABI_NODE_LOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/node.h"
#include "mesh/abi/fields.h"

/* ---------------------------------------------------------------- node load records */
/* How much of the GPU meshd's work actually gets, measured from inside it.
 * CUDA (mesh-rt): a node's idle poll loop reads %globaltimer every turn
 * (a turn is ~1-5 us); a gap over GAP_NS between two turns is time the
 * context was off the GPU (time-sliced out by another context: the driver's
 * slices are ~2 ms).  The record sums the wall time observed and the gaps, so
 * share = 1 - lost / observed.  Only idle nodes observe: a node always in a
 * task observes nothing (its tasks just take longer).  Metal (the lease
 * lanes, node 0's record): observed = commit -> completed of each command
 * buffer, lost = that minus its GPU time (GPUEndTime - GPUStartTime), gaps =
 * command buffers, NL_QUEUE_US = commit -> GPUStartTime.  Written by the node
 * (lane 0) or the lane only; meshd reads deltas.  They persist across
 * relaunches (meshd zeroes them when a slot is loaded). */
#define MESH_NODE_LOAD 0x40000
#define MESH_NL_STRIDE 0x20
#define MESH_NL_OBS_NS 0x00 /* u64 wall ns observed */
#define MESH_NL_LOST_NS 0x08 /* u64 of those, ns lost (CUDA: in gaps > GAP_NS; Metal: round trip - GPU time) */
#define MESH_NL_GAPS 0x10 /* u32 gaps (CUDA) / command buffers (Metal) */
#define MESH_NL_MAX_GAP_NS 0x14 /* u32 the longest gap / lost time of one command buffer in window NL_WIN */
#define MESH_NL_WIN 0x18 /* u32 the H_LOAD_WIN value NL_MAX_GAP_NS belongs to */
#define MESH_NL_QUEUE_US 0x1c /* u32 Metal: commit -> GPU start, us, summed (wraps; deltas); CUDA 0 */
/* The shortest gap counted as time off the GPU (the probe's threshold: an
 * idle turn never takes this long, a time slice always does). */
#define MESH_GAP_NS 20000u
#define MESH_NODE_LOAD_FIELDS_LEN 6
extern const mesh_field MESH_NODE_LOAD_FIELDS[6];

/* Node n of slot s's load record. */
static inline size_t mesh_node_load(uint32_t slot, uint32_t node) {
    return MESH_NODE_LOAD + MESH_NL_STRIDE * (MESH_PROG_MAX_NODES * (size_t)slot + (size_t)node);
}

#endif
