/* @ajax/mesh-abi-program-table: The program table (PT_*): one entry per slot, its states, policies, flags and arg modes.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PROGRAM_TABLE_H
#define MESH_ABI_PROGRAM_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"

#define MESH_PROG_TABLE 0x1000
#define MESH_PT_BYTES 0x80
/* Entry fields (offsets within the entry), all u32 unless stated.  Written by */
/* meshd only while the slot's kernel is not running (load/upgrade happen in a */
/* blink), under the seqlock PT_GEN: odd while being rewritten, even when stable. */
#define MESH_PT_STATE 0x00 /* ST_* */
#define MESH_PT_GEN 0x04
#define MESH_PT_NAME 0x08 /* [u8; 16], NUL padded, [a-z0-9-]{1,15} */
#define MESH_PT_VIEW_OFF 0x18 /* mailbox offset of the view (in the view heap), 0 when view_bytes = 0 */
#define MESH_PT_VIEW_BYTES 0x1c
#define MESH_PT_SNAP_BYTES 0x20 /* view prefix the snapshot copies (<= view_bytes) */
#define MESH_PT_NODES 0x24 /* blocks of the kernel (<= PROG_MAX_NODES) */
#define MESH_PT_WARPS 0x28 /* warps per node (block): 32 x warps threads, 1..=MAX_WARPS */
#define MESH_PT_POLICY 0x2c /* POLICY_* */
#define MESH_PT_HZ 0x30 /* periodic only */
#define MESH_PT_FANOUT 0x34 /* tasks per publish */
#define MESH_PT_PEER 0x38 /* PEER_MODE_* */
#define MESH_PT_READS_MASK 0x3c /* bit s: may read slot s's view (READS_ALL: every slot) */
#define MESH_PT_INPUT_OFF 0x40 /* view offset of the input queue (IQ_*), 0 + len 0 = none */
#define MESH_PT_INPUT_LEN 0x44 /* data bytes (a power of two) */
#define MESH_PT_REQ_OFF 0x48 /* view offset/max of the request window (on_demand bytes) */
#define MESH_PT_REQ_MAX 0x4c
#define MESH_PT_RESP_OFF 0x50
#define MESH_PT_RESP_MAX 0x54
#define MESH_PT_PTX_HASH 0x58 /* u64 fnv1a64 of the PTX bytes */
#define MESH_PT_REGS 0x60 /* cuFuncGetAttribute(NUM_REGS) of mesh_main */
#define MESH_PT_CODE_BYTES 0x64 /* cubin bytes */
#define MESH_PT_VIEW_ABI 0x68
#define MESH_PT_FLAGS 0x6c /* FL_* */
#define MESH_PT_ARG_MODE 0x70 /* ARG_* */
#define MESH_PT_ARG_OFF 0x74 /* view offset for ARG_VIEW_U32 */
#define MESH_PT_RT_ABI 0x78 /* the module's MESH_RT_ABI as read at load */
#define MESH_PT_LOADS 0x7c /* images loaded into this slot (1 + upgrades + rollbacks) */
#define MESH_ST_EMPTY 0u
#define MESH_ST_LOADED 1u
#define MESH_ST_RUNNING 2u
#define MESH_ST_STOPPING 3u
#define MESH_ST_FAILED 4u
#define MESH_STATES_LEN 5
extern const char *const MESH_STATES[5];
#define MESH_POLICY_CONTINUOUS 0u
#define MESH_POLICY_PERIODIC 1u
#define MESH_POLICY_ON_DEMAND 2u
/* `policy = "job"` (plan F5): meshd publishes nothing on its own; the
 * program's tasks are the items of the fleet's jobs (OP_JOB), leased to this
 * member by the leader (mesh-host jobs.rs), plus `task`/`req` on demand. */
#define MESH_POLICY_JOB 3u
#define MESH_POLICIES_LEN 4
extern const char *const MESH_POLICIES[4];
#define MESH_PEER_MODE_NONE 0u
#define MESH_PEER_MODE_LOCKSTEP 1u
#define MESH_PEER_MODES_LEN 2
extern const char *const MESH_PEER_MODES[2];
#define MESH_READS_ALL 0xffffffffu
/* `in_flight = "batch"`: the next publish waits until the whole previous batch completed. */
#define MESH_FL_BATCH 1u
#define MESH_FL_AUTOSTART 2u
/* Task arg: the index 0..fanout-1, or the u32 at view offset PT_ARG_OFF. */
#define MESH_ARG_INDEX 0u
#define MESH_ARG_VIEW_U32 1u

/* Slot s's entry in the program table. */
static inline size_t mesh_prog_entry(uint32_t slot) { return MESH_PROG_TABLE + MESH_PT_BYTES * (size_t)slot; }

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_POLICIES_LEN == MESH_POLICY_JOB  + 1, "POLICIES.len() == POLICY_JOB as usize + 1");
_Static_assert(MESH_PT_LOADS + 4 <= MESH_PT_BYTES, "PT_LOADS + 4 <= PT_BYTES");
_Static_assert((MESH_PT_PTX_HASH % 8 == 0), "PT_PTX_HASH.is_multiple_of(8)");

#endif
