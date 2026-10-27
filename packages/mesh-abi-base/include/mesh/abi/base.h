/* @ajax/mesh-abi-base: Mailbox-wide constants of the mesh contract: magic, version, runtime ABI, warp and shared-memory sizes, slots, systems, names.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_BASE_H
#define MESH_ABI_BASE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_MAGIC 0x4853454du /* "MESH" */
/* Threads per warp (a CUDA warp, an Apple SIMD-group).  A node -- one block
 * (threadgroup) of a program's kernel -- is PT_WARPS such warps: 32 x warps
 * threads (manifest `warps`, 1..=MAX_WARPS); a program's kernel has `nodes`
 * such blocks. */
#define MESH_NODE_THREADS 32u
/* The most warps a node may have (1024 threads: CUDA's and Metal's block limit). */
#define MESH_MAX_WARPS 32u
/* The runtime's own shared (threadgroup) memory at the start of every node's
 * block: the ticket descriptor thread 0 broadcasts and the block
 * collectives' scratch (mesh-rt, mesh_rt.h).  A program's `shared_bytes`
 * follow it: the launch's dynamic shared memory is RT_SHARED_BYTES +
 * shared_bytes, and `ctx.shared(off, ..)` offsets are relative to the
 * program's part. */
#define MESH_RT_SHARED_BYTES 0x200u
/* The largest manifest `shared_bytes` (the device's own limit applies at load:
 * CUDA's opt-in shared memory per block, Metal's maxThreadgroupMemoryLength). */
#define MESH_MAX_SHARED_BYTES ((uint32_t)(0x38000 - MESH_RT_SHARED_BYTES))
/* Shared memory sizes are multiples of this (Metal's threadgroup memory granule). */
#define MESH_SHARED_ALIGN 16u
/* H_PEER of a single machine. */
#define MESH_PEER_NONE 0xffffffffu
#define MESH_VERSION 2u
/* The value of a program's `MESH_RT_ABI` global and its manifest's `abi`.
 * 3: the node loop keeps a node load record (NL_*) and reads H_LOAD_WIN.
 * 4: a lockstep program's exchange runs over its *group* (H_GROUP_RANK,
 * H_GROUP_N: `ctx.group()`), not H_PEER / H_SYSTEMS.
 * 5: pipes have two rings per inbox (GPU side and CPU side), the record sum
 * folds in the position, pushes count themselves in PP_FLIGHT/PP_FLIGHT1 under
 * H_PIPE_EPOCH and answer Busy under PP_IN_HOLD; Metal pops a copy (`pop()`).
 * An image built against 4 would corrupt a 5 inbox, so it is refused at load.
 * 6: multi-warp nodes.  A node is a block of 32 x PT_WARPS threads with
 * RT_SHARED_BYTES + PX_SHARED bytes of dynamic shared memory; the node loop
 * runs at block scope (thread 0 claims and polls, the descriptor reaches the
 * block through shared memory and a block barrier; the block finishes the
 * task, a barrier, then thread 0 completes it).  PT_SMS is gone (PT_WARPS). */
#define MESH_RT_ABI 6u
#define MESH_MAILBOX_BYTES 0x800000
/* Every program's kernel entry (emitted by `mesh_program!`). */
#define MESH_PROGRAM_ENTRY "mesh_main"
/* The `#[no_mangle]` u32 global meshd reads with `cuModuleGetGlobal` at load. */
#define MESH_RT_ABI_SYMBOL "MESH_RT_ABI"
/* The largest cluster: system ids 0..MAX_SYSTEMS-1. */
#define MESH_MAX_SYSTEMS 8u
/* ---------------------------------------------------------------- program table */
#define MESH_PROG_SLOTS 32
#define MESH_NAME_BYTES 16
#define MESH_NAME_MAX 15

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_PROG_SLOTS <= 32, "PROG_SLOTS <= 32");
_Static_assert(MESH_MAX_SYSTEMS <= 16, "MAX_SYSTEMS <= 16");
_Static_assert((MESH_RT_SHARED_BYTES % MESH_SHARED_ALIGN == 0), "RT_SHARED_BYTES.is_multiple_of(SHARED_ALIGN)");
_Static_assert((MESH_MAX_SHARED_BYTES % MESH_SHARED_ALIGN == 0), "MAX_SHARED_BYTES.is_multiple_of(SHARED_ALIGN)");
_Static_assert(MESH_MAX_WARPS * MESH_NODE_THREADS == 1024, "MAX_WARPS * NODE_THREADS == 1024");

#endif
