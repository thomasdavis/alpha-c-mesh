/* @ajax/mesh-abi-control-block: A program's control block (P_*): shutdown, run state, faults, the request window, worker words, counters.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_CONTROL_BLOCK_H
#define MESH_ABI_CONTROL_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* ---------------------------------------------------------------- control block + ring */
/* Program s's control block at `prog_ctl(s)`, PROG_CTL_BYTES.  The hot words
 * (ticket head; completions; program counters) each own a 128-byte line, so
 * programs do not share one line (a shared line capped v1 at ~40k tasks/s).  The
 * cold words share line 0 (the block has 16 lines and the ring, results and
 * marks need 12). */
#define MESH_PROG_CTL 0x2000
#define MESH_PROG_CTL_BYTES 0x800
#define MESH_LINE 0x80
/* line 0: cold */
/* u32 host: nonzero -> every node exits at its next task boundary.
 * SHUTDOWN_HALT for a halt the slot comes back from (a blink, a rebind),
 * SHUTDOWN_EXIT for a stop, a failure or meshd's exit (a native worker's
 * process then exits too; a GPU kernel treats both alike). */
#define MESH_P_SHUTDOWN 0x00
#define MESH_SHUTDOWN_HALT 1u
#define MESH_SHUTDOWN_EXIT 2u
#define MESH_P_ONLINE 0x04 /* u32 GPU atom: nodes online (host zeroes before a launch) */
#define MESH_P_HALTED 0x08 /* u32 GPU atom: nodes halted */
/* u32 host only: the run state meshd wants (1 run / 0 stop).  Plain host
 * stores; the GPU only reads it. */
#define MESH_P_RUN 0x0c
/* u32 GPU only: a GPU program's run request (READS_ALL, i.e. http's
 * /start, /stop): `n << 1 | on`, n counting requests, advanced with a GPU
 * atom.cas (`run_req_next`); meshd only reads it and honours a changed n on
 * its next tick (then stores P_RUN).  meshd never writes this word, not even
 * at load: it notes its value instead.  So every word is read-modify-written
 * by one side only -- required where host and GPU RMWs on the same word are
 * not mutually atomic (CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED = 0,
 * e.g. an RTX 3060 over PCIe).  Mailbox rule: the host RMWs only host-only
 * words (H_PROG_SEQ, P_LAUNCHES); it stores to GPU-atomic words (P_ONLINE,
 * P_HALTED, P_RING_HEAD, P_FAULT, the view) only while that program's kernel
 * is halted. */
#define MESH_P_RUN_REQ 0x10
/* u32 GPU: the first fault the runtime caught (FAULT_*), 0 none; meshd marks the program FAILED. */
#define MESH_P_FAULT 0x14
#define MESH_P_RING_TAIL 0x18 /* u64 host: tickets published (informational) */
/* The request window handshake (on_demand byte requests): host writes the
 * request bytes at view PT_REQ_OFF, P_REQ_LEN, then P_REQ_SEQ += 1 (release)
 * and publishes OP_REQ; the program writes the response at PT_RESP_OFF,
 * P_RESP_LEN, then P_RESP_SEQ = P_REQ_SEQ (release) (`Ctx::respond`). */
#define MESH_P_REQ_SEQ 0x20 /* u64 */
#define MESH_P_RESP_SEQ 0x28 /* u64 */
#define MESH_P_REQ_LEN 0x30 /* u32 */
#define MESH_P_RESP_LEN 0x34 /* u32 */
#define MESH_P_LAUNCHES 0x38 /* u32 host: launches of this slot (informational) */
/* A native worker's words (PX_EXEC = BACKEND_NATIVE): host-only, 0 for any */
/* other program; meshd refreshes them every tick.  Its CPU time and RSS are */
/* load figures: pload(s) PL_HOST (PL_CPU_NS ..), like a wasm slot's. */
#define MESH_P_PID 0x40 /* u32 the running image's worker process (0: none) */
#define MESH_P_SPAWNS 0x44 /* u32 processes started for the current image */
#define MESH_P_CRASHES 0x48 /* u32 of those, the ones that died while running (FAILED) */
#define MESH_P_EXIT 0x4c /* u32 the last exit: status << 8 | signal (0: none yet) */
#define MESH_P_LIMITS 0x50 /* u32 LIMIT_* bits actually applied to the process */
#define MESH_LIMIT_CPU 1u /* Linux: systemd scope CPUQuota (checked in its cgroup) */
#define MESH_LIMIT_MEM 2u /* Linux: MemoryMax + MemorySwapMax = 0 (checked in its cgroup) */
#define MESH_LIMIT_NICE 4u
#define MESH_LIMIT_BG 8u /* macOS: PRIO_DARWIN_BG (taskpolicy -b); Linux: SCHED_IDLE */
/* line 1 */
#define MESH_P_RING_HEAD 0x80 /* u32 GPU atom.add -> ticket */
/* line 2 */
#define MESH_P_DONE 0x100 /* u32 GPU red.add per completion */
#define MESH_P_TRACE_N 0x104 /* u32 GPU atom.add per completion: trace records written */
/* line 3 */
#define MESH_P_COUNTERS 0x180 /* u32 x COUNTERS, program-defined (Ctx::count) */
#define MESH_COUNTERS 4
/* The control block's cold line (P_*, line 0: host and GPU words). */
#define MESH_CTL_FIELDS_LEN 17
extern const mesh_field MESH_CTL_FIELDS[17];

/* Slot s's control block. */
static inline size_t mesh_prog_ctl(uint32_t slot) { return MESH_PROG_CTL + MESH_PROG_CTL_BYTES * (size_t)slot; }
/* The next P_RUN_REQ value after `cur` for a request to run (`on`) or stop. */
static inline uint32_t mesh_run_req_next(uint32_t cur, bool on) { return ((cur & ~1u) + 2u) | (uint32_t)on; }

#endif
