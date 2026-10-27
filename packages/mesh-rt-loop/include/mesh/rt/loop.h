/* @ajax/mesh-rt-loop: the node loop, the body of every program's kernel (`mesh_main`).
 *
 * At block scope: thread 0 of the node takes a ticket from the slot's ring and
 * waits for its publication (acquire), then writes the descriptor (ticket, op,
 * arg, completion slot, or "exit") to the runtime's shared area; a block
 * barrier hands it to every thread (thread 0's sys-scope acquire, then the
 * barrier, puts every thread's reads after the host's payload). The whole
 * block runs the op; a barrier (every thread's outputs before it), then thread
 * 0 completes it: result, release of the mark, P_DONE, the node record and
 * the trace. The node exits at a task boundary once P_SHUTDOWN is set (the
 * decision is thread 0's, broadcast like a ticket, so every thread leaves the
 * loop at the same barrier), and takes no new ticket while H_BLINK is set.
 *
 * While idle, thread 0 also measures how much of the GPU the context gets
 * (the node load records): every poll turn reads the global timer, and a turn
 * that took over GAP_NS was time the context spent off the GPU.
 * Ported from alpha-rust-mesh crates/mesh-rt/src/ring.rs. */
#ifndef MESH_RT_LOOP_H
#define MESH_RT_LOOP_H

#include <stdint.h>

#include "mesh/abi/control_block.h"
#include "mesh/abi/header.h"
#include "mesh/abi/node.h"
#include "mesh/abi/node_load.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/trace.h"
#include "mesh/rt/block.h"
#include "mesh/rt/ctx.h"
#include "mesh/rt/prim.h"

/* Idle poll period (publish->start latency is what the host sees); heartbeat
 * at most every HEARTBEAT_NS while idle. */
#define MESH_RT_POLL_NS 1000u
#define MESH_RT_HEARTBEAT_NS UINT64_C(2000000)
#define MESH_RT_SHUTDOWN_CHECK_NS UINT64_C(20000)

typedef void (*mesh_init_fn)(const mesh_ctx *ctx);
typedef uint32_t (*mesh_task_fn)(const mesh_ctx *ctx, uint32_t op, uint32_t arg);

MESH_RT_INLINE uint8_t *mesh_rt_entry(uint8_t *ctl, uint32_t t) { return ctl + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING); }

/* The body of mesh_main: mb is the mailbox's device address and slot a slot meshd set up. */
MESH_RT_INLINE void mesh_rt_run(uint8_t *mb, uint32_t slot, mesh_init_fn init, mesh_task_fn task) {
    mesh_ctx ctx = mesh_ctx_new(mb, slot);
    /* Thread 0 of the node does everything the host sees (l == 0 below). */
    uint32_t l = ctx.thread;
    uint32_t node = ctx.node;
    uint8_t *desc = mesh_rt_smem_base() + MESH_RT_DESC;
    uint8_t *ctl = mb + mesh_prog_ctl(slot);
    /* node_rec(slot, node) as one 32-bit index times NODE_STRIDE: ptxas 13.0
     * (sm_121) miscompiles mb + (slot << 10) + node * 32 on its uniform
     * datapath, dropping the high word of mb; a single wide multiply-add is
     * compiled correctly. */
    uint8_t *rec = mb + MESH_PROG_NODES + (size_t)MESH_NODE_STRIDE * (slot * (uint32_t)MESH_PROG_MAX_NODES + node);
    /* The node load record, indexed the same way (one wide multiply-add). */
    uint8_t *lrec = mb + MESH_NODE_LOAD + (size_t)MESH_NL_STRIDE * (slot * (uint32_t)MESH_PROG_MAX_NODES + node);
    /* Thread 0's running sums, continued across relaunches like the heartbeat. */
    uint64_t obs = 0, lost = 0;
    uint32_t gaps = 0, max_gap = 0, win = 0;
    if (l == 0) {
        obs = mesh_rt_rd64(lrec + MESH_NL_OBS_NS);
        lost = mesh_rt_rd64(lrec + MESH_NL_LOST_NS);
        gaps = mesh_rt_rd(lrec + MESH_NL_GAPS);
        max_gap = mesh_rt_rd(lrec + MESH_NL_MAX_GAP_NS);
        win = mesh_rt_rd(lrec + MESH_NL_WIN);
        mesh_rt_wr(rec + MESH_N_SM, mesh_rt_smid());
        mesh_rt_fence_acq_rel();
        mesh_rt_atom_add(ctl + MESH_P_ONLINE, 1);
    }
    /* The heartbeat continues across relaunches (viewers diff it). */
    uint32_t beat = l == 0 ? mesh_rt_rd(rec + MESH_N_HEARTBEAT) : 0;
    for (;;) {
        uint32_t t = 0, go = 1;
        if (l == 0) {
            bool have = false;
            uint64_t beat_at = mesh_rt_gtimer(), check_at = beat_at, prev = beat_at;
            /* Poll relaxed (an acquire per poll costs fabric bandwidth idle nodes
             * share); shutdown and heartbeat go by the clock. */
            for (;;) {
                if (!have && mesh_rt_ld_relaxed(mb + MESH_H_BLINK) == 0) {
                    t = mesh_rt_atom_add(ctl + MESH_P_RING_HEAD, 1);
                    have = true;
                }
                uint8_t *seq = mesh_rt_entry(ctl, t) + MESH_E_SEQ;
                if (have && mesh_rt_ld_relaxed(seq) == t + 1) break;
                uint64_t now = mesh_rt_gtimer();
                /* A turn is a few us; one over GAP_NS is a time slice the context did not get. */
                uint64_t d = now - prev;
                prev = now;
                obs += d;
                if (d > (uint64_t)MESH_GAP_NS) {
                    lost += d;
                    gaps += 1;
                    uint32_t w = mesh_rt_ld_relaxed(mb + MESH_H_LOAD_WIN);
                    if (w != win) { win = w; max_gap = 0; }
                    uint32_t d32 = d > UINT32_MAX ? UINT32_MAX : (uint32_t)d;
                    if (d32 > max_gap) max_gap = d32;
                    mesh_rt_wr(lrec + MESH_NL_MAX_GAP_NS, max_gap);
                    mesh_rt_wr(lrec + MESH_NL_WIN, win);
                    mesh_rt_wr(lrec + MESH_NL_GAPS, gaps);
                    mesh_rt_wr64(lrec + MESH_NL_LOST_NS, lost);
                    mesh_rt_wr64(lrec + MESH_NL_OBS_NS, obs);
                }
                if (now - check_at >= MESH_RT_SHUTDOWN_CHECK_NS) {
                    check_at = now;
                    /* A ticket published before the shutdown still runs. */
                    if (mesh_rt_ld_relaxed(ctl + MESH_P_SHUTDOWN) != 0 && !(have && mesh_rt_ld_relaxed(seq) == t + 1)) {
                        go = 0;
                        break;
                    }
                }
                if (now - beat_at >= MESH_RT_HEARTBEAT_NS) {
                    beat_at = now;
                    beat += 1;
                    mesh_rt_st_relaxed(rec + MESH_N_HEARTBEAT, beat);
                    mesh_rt_wr64(lrec + MESH_NL_OBS_NS, obs);
                }
                mesh_rt_nanosleep(MESH_RT_POLL_NS);
            }
            mesh_rt_wr64(lrec + MESH_NL_OBS_NS, obs);
            /* The descriptor: the published entry, read after the acquire. */
            mesh_rt_wr(desc + MESH_RT_D_GO, go);
            if (go != 0) {
                uint8_t *e = mesh_rt_entry(ctl, t);
                (void)mesh_rt_ld_acquire(e + MESH_E_SEQ);
                mesh_rt_wr(desc + MESH_RT_D_T, t);
                mesh_rt_wr(desc + MESH_RT_D_OP, mesh_rt_rd(e + MESH_E_OP));
                mesh_rt_wr(desc + MESH_RT_D_ARG, mesh_rt_rd(e + MESH_E_ARG));
                mesh_rt_wr(desc + MESH_RT_D_CSLOT, mesh_rt_rd(e + MESH_E_SLOT));
            }
        }
        /* Thread 0's acquire, then the block barrier, puts every thread's reads after the host's payload. */
        mesh_rt_bar_sync();
        if (mesh_rt_rd(desc + MESH_RT_D_GO) == 0) break;
        uint32_t tk = mesh_rt_rd(desc + MESH_RT_D_T), op = mesh_rt_rd(desc + MESH_RT_D_OP), arg = mesh_rt_rd(desc + MESH_RT_D_ARG);
        uint32_t cslot = mesh_rt_rd(desc + MESH_RT_D_CSLOT) % MESH_PROG_RESULTS;

        ctx.ticket = tk;
        uint64_t start = mesh_rt_gtimer();
        uint32_t result;
        if (op == MESH_OP_INIT) { init(&ctx); result = MESH_R_NONE; }
        else result = task(&ctx, op, arg);

        /* The barrier orders every thread's outputs before thread 0's release of
         * the mark (and lets thread 0 rewrite the descriptor: every thread has read it). */
        mesh_rt_bar_sync();
        if (l == 0) {
            uint64_t end = mesh_rt_gtimer();
            mesh_rt_wr(ctl + MESH_P_RESULTS + 4 * cslot, result);
            mesh_rt_st_release(ctl + MESH_P_MARKS + 4 * cslot, tk + 1);
            mesh_rt_red_add(ctl + MESH_P_DONE, 1);
            mesh_rt_wr(rec + MESH_N_TASKS, mesh_rt_rd(rec + MESH_N_TASKS) + 1);
            mesh_rt_wr(rec + MESH_N_LAST_OP, op);
            mesh_rt_wr64(rec + MESH_N_BUSY_NS, mesh_rt_rd64(rec + MESH_N_BUSY_NS) + (end - start));
            beat += 1;
            mesh_rt_st_relaxed(rec + MESH_N_HEARTBEAT, beat);
            uint32_t n = mesh_rt_atom_add(ctl + MESH_P_TRACE_N, 1);
            uint8_t *tr = mb + mesh_trace_rec(slot, n);
            mesh_rt_wr(tr, slot | node << 8 | op << 16);
            mesh_rt_wr(tr + 4, tk);
            mesh_rt_wr(tr + 8, (uint32_t)start);
            mesh_rt_wr(tr + 12, (uint32_t)end);
        }
    }
    /* Every thread left at the same barrier (go == 0 for all). */
    if (l == 0) {
        mesh_rt_fence_acq_rel();
        mesh_rt_atom_add(ctl + MESH_P_HALTED, 1);
    }
}

#endif
