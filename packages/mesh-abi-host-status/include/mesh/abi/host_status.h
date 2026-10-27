/* @ajax/mesh-abi-host-status: The host status block's first region: uptime, peers, rates, names, isolation, cluster size.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_STATUS_H
#define MESH_ABI_HOST_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* host status block (offsets within it) */
#define MESH_S_UPTIME_MS 0x00 /* u64 */
#define MESH_S_EPOCH 0x08 /* u32 = H_EPOCH (blinks) */
#define MESH_S_SYSTEM 0x0c /* u32 0..N-1, PEER_NONE single */
#define MESH_S_PEER_STATE 0x10 /* u32 0 single, 1 waiting (no leader this member can reach: no majority), 2 up (a leader with a majority) */
#define MESH_S_PROGRAMS 0x14 /* u32 non-empty slots */
#define MESH_S_SMS_TOTAL 0x18 /* u32 */
#define MESH_S_GREEN 0x1c /* u32 1: meshd's context is a green context */
#define MESH_S_TASKS_PER_S 0x20 /* f32, all programs, last second */
#define MESH_S_HEAP_FREE 0x24 /* u32 free view-heap pages */
#define MESH_S_SMS 0x28 /* u32 SMs of meshd's context */
#define MESH_S_PEER_RTT_US 0x2c /* f32 */
/* The lockstep program's progress (its next task arg as meshd computes it,
 * u64 with carry) and S_LOCK_VIEW, written together under one lock at each of
 * its completions: a consistent pair (the view itself changes in place). */
#define MESH_S_LOCK_STEP 0x30 /* u64 */
#define MESH_S_HTTP_REQUESTS 0x38 /* u32 requests relayed (control + TCP) */
#define MESH_S_BLINK_US 0x3c /* u32 the last blink, H_BLINK set to cleared */
#define MESH_S_HOSTNAME 0x40 /* 32 bytes NUL padded */
#define MESH_S_GPU 0x60 /* 32 bytes */
#define MESH_S_PROGRAMS_DIR 0x80 /* 64 bytes, --programs DIR */
#define MESH_S_LAST_HTTP 0xc0 /* 64 bytes, the last request line */
#define MESH_S_HTTP_ADDR 0x100 /* 64 bytes */
#define MESH_S_LOCK_VIEW 0x140 /* 32 bytes: the lockstep program's view prefix */
#define MESH_S_LOCK_NAME 0x160 /* 16 bytes: the lockstep program's name, NUL padded */
#define MESH_S_RATES 0x170 /* f32 x PROG_SLOTS: tasks/s per slot, last second */
#define MESH_S_LAST_BLINK_MS 0x1f0 /* u64 uptime ms at the last blink */
#define MESH_S_ISOLATION 0x1f8 /* u32 ISOLATION_*: how meshd runs the programs (--isolation) */
/* One CUDA context; a module change blinks every program (the default). */
#define MESH_ISOLATION_BLINK 0u
/* The cluster (0x200..): its size, this machine's capabilities, and one */
/* record per member as this meshd sees it (its own included). */
#define MESH_S_SYSTEMS 0x200 /* u32 N (1 single) */
#define MESH_S_LINKED 0x204 /* u32 bit k: system k is linked (JOINed), this one included */
/* 0x208, 0x20c: unused (this machine's cc and blocks/SM are the header's */
/* H_CC and H_BLOCKS_PER_SM, which every snapshot carries). */
#define MESH_S_ARCH 0x210 /* 16 bytes: the host's CPU architecture (aarch64, x86_64) */
#define MESH_S_LOCK_WAIT_US 0x220 /* f32: mean wait per exchange round for the slowest member, last second */
#define MESH_S_SESSIONS 0x224 /* u32 cluster sessions that reached UP */
/* One MPS client process per loaded image under meshd's private MPS daemon:
 * module changes halt no other program. */
#define MESH_ISOLATION_MPS 1u
/* Metal: leases (no resident kernels); a module change halts only its slot. */
#define MESH_ISOLATION_LEASE 2u

#endif
