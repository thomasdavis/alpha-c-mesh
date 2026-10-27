/* @ajax/mesh-abi-header: The mailbox header (H_*): 0x0000..0x0100.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HEADER_H
#define MESH_ABI_HEADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* ---------------------------------------------------------------- header */
/* 0x0000 .. 0x0100.  Line 0 is read-mostly; TRACE has no global counter in v2 */
/* (each program counts its own records, P_TRACE_N), so nothing here is hot. */
#define MESH_H_MAGIC 0x00 /* u32 MAGIC */
#define MESH_H_VERSION 0x04 /* u32 VERSION */
#define MESH_H_PEER 0x18 /* u32 this system 0..H_SYSTEMS-1 of a cluster, or PEER_NONE */
/* u32, host: 1 while a blink is in progress.  Nodes take no new ticket while
 * it is set (they keep checking P_SHUTDOWN). */
#define MESH_H_BLINK 0x20
/* u32, host: bumped at the end of every blink (module set changed). */
#define MESH_H_EPOCH 0x24
/* u32, host: bumped after every program-table change (viewers re-read the table). */
#define MESH_H_PROG_SEQ 0x28
/* u32, host: the device's SM count (mesh-rt's `Ctx::sms`). */
#define MESH_H_SMS 0x2c
/* u32, host: the fleet's size N (1 single, 2..=MAX_SYSTEMS in a fleet), set
 * before any program's OP_INIT; with H_PEER the fleet's ids (viewers, http).
 * Lockstep programs read their group (H_GROUP_*). */
#define MESH_H_SYSTEMS 0x30
/* u32, host: the device's compute capability, major * 10 + minor (GB10 121,
 * RTX 3060 86).  With H_BLOCKS_PER_SM, H_SMS_TOTAL and each program's PT_REGS
 * / PX_PER_SM these are the per-machine facts a heterogeneous cluster shows.
 * The header is the one source of truth for them: the snapshot carries the
 * header (viewers read them there) and F_JOIN's caps are read from it. */
#define MESH_H_CC 0x34
/* u32, host: CU_DEVICE_ATTRIBUTE_MAX_BLOCKS_PER_MULTIPROCESSOR (GB10 24, sm_86 16). */
#define MESH_H_BLOCKS_PER_SM 0x38
/* u32, host: the device's SM count (H_SMS is the SMs meshd's context runs on). */
#define MESH_H_SMS_TOTAL 0x3c
/* u32, host: which GPU backend runs the programs, BACKEND_* in bits 0..8 and
 * the device family in bits 8..16 (Metal: the highest MTLGPUFamilyAppleN the
 * device supports, M3 Ultra 9; CUDA: 0).  The word is 0 on a CUDA machine
 * (BACKEND_CUDA), so every CUDA header reads as before.  A non-CUDA device
 * has no compute capability: H_CC is 0 there, and H_SMS / H_SMS_TOTAL count
 * its compute units instead of SMs (Metal: GPU cores), H_BLOCKS_PER_SM is 0
 * (no resident blocks: Metal runs leases, see BACKEND_METAL). */
#define MESH_H_BACKEND 0x40
/* u32, host: the device's maximum threads per threadgroup / block (Metal:
 * maxThreadsPerThreadgroup, M3 Ultra 1024; CUDA: MAX_THREADS_PER_BLOCK, 1024). */
#define MESH_H_MAX_TG_THREADS 0x44
/* u32, host: the load window, bumped by meshd's load sampler about once a
 * second.  A node that sees it change starts a new longest-gap word
 * (NL_MAX_GAP_NS, NL_WIN). */
#define MESH_H_LOAD_WIN 0x48
/* u32 each, host: this member's lockstep group (fleet plan F3): its rank in
 * the group's ordered member list, the group's size, and the group's epoch.
 * A lockstep program exchanges with the group (ex_counter(rank),
 * ex_slots(rank); it sums ranks 0..N-1 in order), never with H_SYSTEMS.  N is
 * 0 when this member is in no group (single, or left out of the current
 * epoch).  meshd writes them only while the lockstep program is halted or
 * drained and unfed (an epoch change), so a task never sees them move. */
#define MESH_H_GROUP_RANK 0x4c
#define MESH_H_GROUP_N 0x50
#define MESH_H_GROUP_EPOCH 0x54
/* u32, host: the pipes' flight epoch (mesh-abi pipe::flight_begin).  A push
 * counts itself in its producer's PP_FLIGHT word of parity H_PIPE_EPOCH & 1;
 * meshd bumps it before it waits for the pushes in flight to end, so the
 * pushes that count in the old parity's word only drain. */
#define MESH_H_PIPE_EPOCH 0x58
/* u32, host: the most shared (threadgroup) memory one node may have, in
 * bytes (CUDA: MAX_SHARED_MEMORY_PER_BLOCK_OPTIN, Metal:
 * maxThreadgroupMemoryLength), the runtime's RT_SHARED_BYTES included.  A
 * manifest whose RT_SHARED_BYTES + shared_bytes exceeds it is refused here. */
#define MESH_H_MAX_SHARED 0x5c
#define MESH_H_BYTES 0x100
/* Every field of a region as (offset, bytes): the compile-time checks
 * require that no two of them alias (two branches once both put a word at
 * header 0x30), and each table's last field must end inside its region. */
#define MESH_HEADER_FIELDS_LEN 19
extern const mesh_field MESH_HEADER_FIELDS[19];

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_H_SMS < MESH_H_SYSTEMS, "H_SMS < H_SYSTEMS");
_Static_assert(MESH_H_SYSTEMS < MESH_H_CC, "H_SYSTEMS < H_CC");
_Static_assert(MESH_H_SMS_TOTAL + 4 <= MESH_H_BACKEND, "H_SMS_TOTAL + 4 <= H_BACKEND");
_Static_assert(MESH_H_MAX_TG_THREADS + 4 <= MESH_H_BYTES, "H_MAX_TG_THREADS + 4 <= H_BYTES");
_Static_assert(MESH_H_SYSTEMS + 4 <= MESH_H_BYTES, "H_SYSTEMS + 4 <= H_BYTES");

#endif
