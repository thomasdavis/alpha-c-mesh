/* @ajax/mesh-rt-ctx: what a program's init and task see (Rust's `Ctx`): its view, other
 * programs' views (gated by the manifest's `reads`), the input queue, pipes,
 * the request window, the exchange page (lockstep only), counters, block
 * collectives, shared memory, scratch, and node/warp/lane ids.
 *
 * The own entry is read once at kernel start under its PT_GEN seqlock: meshd
 * rewrites it only while the slot's kernel is not running. Other slots'
 * entries can change under a running reader, so peer_view reads them under
 * their seqlock.
 *
 * Warp collectives (input_pop, respond, push_with, pop_with, pipe_len,
 * pipe_free) must be called by all 32 lanes of a warp, never after a branch
 * the lanes may take differently; the calling warp's lane 0 acts. In a
 * multi-warp node call them from one warp (`if (mesh_ctx_warp(c) == 0)`).
 * Block collectives must be called by every thread of the node. Everything
 * else may be called by any thread. View and shared accessors are bounds
 * checked (an out-of-range access reads 0 / drops the write, and P_FAULT).
 *
 * Ids: node (the block, 0..nodes), warp (0..warps), lane (0..32 in the warp),
 * thread (0..threads = 32 x warps); leader is thread 0 of the node, which
 * makes side effects.
 * Ported from alpha-rust-mesh crates/mesh-rt/src/ctx.rs. */
#ifndef MESH_RT_CTX_H
#define MESH_RT_CTX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/ext.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/header.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/pipe_ring.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/words.h"
#include "mesh/rt/block.h"
#include "mesh/rt/prim.h"
#include "mesh/rt/view.h"

/* ---- the mailbox's words for the pipe algorithm (system scope: the other end may be a CPU) */
static inline uint32_t mesh_rt_gw_load(void *c, size_t a) { return mesh_rt_ld_acquire((uint8_t *)c + a); }
static inline void mesh_rt_gw_store(void *c, size_t a, uint32_t v) { mesh_rt_st_release((uint8_t *)c + a, v); }
static inline uint32_t mesh_rt_gw_cas(void *c, size_t a, uint32_t cmp, uint32_t desired) {
    return mesh_rt_atom_cas((uint8_t *)c + a, cmp, desired);
}
static inline uint32_t mesh_rt_gw_add(void *c, size_t a, uint32_t v) { return mesh_rt_atom_add((uint8_t *)c + a, v); }
static inline void mesh_rt_gw_fence(void *c) { (void)c; mesh_rt_fence_sc(); }
MESH_RT_INLINE mesh_words mesh_rt_gw(uint8_t *mb) {
    mesh_words w = {mb, mesh_rt_gw_load, mesh_rt_gw_store, mesh_rt_gw_cas, mesh_rt_gw_add, mesh_rt_gw_fence, NULL, NULL};
    return w;
}

/* The exchange page (EX_BASE) for the lockstep program: system k's counter and
 * slots (mesh_ex_counter / mesh_ex_slots relative to EX_BASE), k < MAX_SYSTEMS. */
typedef struct {
    uint8_t *base;
} mesh_exchange;
MESH_RT_INLINE uint8_t *mesh_exchange_base(mesh_exchange x) { return x.base; }
/* System k's u64 round counter. */
MESH_RT_INLINE uint8_t *mesh_exchange_counter(mesh_exchange x, uint32_t k) { return x.base + (mesh_ex_counter(k) - MESH_EX_BASE); }
/* System k's two EX_SLOT_BYTES slots (parity 0, 1). */
MESH_RT_INLINE uint8_t *mesh_exchange_slots(mesh_exchange x, uint32_t k) { return x.base + (mesh_ex_slots(k) - MESH_EX_BASE); }

typedef struct {
    uint8_t *mb;
    uint8_t *ctl;
    mesh_view view;
    uint32_t slot;
    uint32_t node;
    uint32_t lane;
    /* The thread in the node (0..threads), its warp, the node's warps. */
    uint32_t thread;
    uint32_t warp;
    uint32_t warps;
    /* The program's shared memory (after the runtime's RT_SHARED_BYTES) and its bytes. */
    uint8_t *smem;
    uint32_t smem_len;
    /* The program's scratch (manifest `scratch_bytes`): device memory of its own; NULL / 0 none. */
    uint8_t *scratch;
    uint32_t scratch_len;
    uint32_t nodes;
    uint32_t reads_mask;
    uint32_t peer_mode;
    uint32_t input_off;
    uint32_t input_len;
    uint32_t req_off;
    uint32_t req_max;
    uint32_t resp_off;
    uint32_t resp_max;
    /* The running task's ticket (set by the node loop before each op). */
    uint32_t ticket;
} mesh_ctx;

/* Reads the slot's table entry under its seqlock (PT_GEN even and unchanged). */
MESH_RT_INLINE mesh_ctx mesh_ctx_new(uint8_t *mb, uint32_t slot) {
    uint8_t *e = mb + mesh_prog_entry(slot);
    uint8_t *ctl = mb + mesh_prog_ctl(slot);
    for (;;) {
        uint32_t g = mesh_rt_ld_acquire(e + MESH_PT_GEN);
        if (g & 1u) { mesh_rt_nanosleep(1000); continue; }
        uint8_t *x = mb + mesh_prog_ext(slot);
        uint32_t dyn = mesh_rt_dyn_smem();
        mesh_ctx c;
        c.mb = mb;
        c.ctl = ctl;
        c.view = (mesh_view){mb + mesh_rt_rd(e + MESH_PT_VIEW_OFF), mesh_rt_rd(e + MESH_PT_VIEW_BYTES), ctl};
        c.slot = slot;
        c.node = mesh_rt_ctaid();
        c.lane = mesh_rt_lane();
        c.thread = mesh_rt_tid();
        c.warp = mesh_rt_tid() >> 5;
        c.warps = mesh_rt_ntid() >> 5;
        c.smem = mesh_rt_smem_base() + MESH_RT_SHARED_BYTES;
        c.smem_len = dyn > MESH_RT_SHARED_BYTES ? dyn - MESH_RT_SHARED_BYTES : 0;
        c.scratch = (uint8_t *)(uintptr_t)mesh_rt_rd64(x + MESH_PX_SCRATCH);
        c.scratch_len = mesh_rt_rd(x + MESH_PX_SCRATCH_BYTES);
        c.nodes = mesh_rt_rd(e + MESH_PT_NODES);
        c.reads_mask = mesh_rt_rd(e + MESH_PT_READS_MASK);
        c.peer_mode = mesh_rt_rd(e + MESH_PT_PEER);
        c.input_off = mesh_rt_rd(e + MESH_PT_INPUT_OFF);
        c.input_len = mesh_rt_rd(e + MESH_PT_INPUT_LEN);
        c.req_off = mesh_rt_rd(e + MESH_PT_REQ_OFF);
        c.req_max = mesh_rt_rd(e + MESH_PT_REQ_MAX);
        c.resp_off = mesh_rt_rd(e + MESH_PT_RESP_OFF);
        c.resp_max = mesh_rt_rd(e + MESH_PT_RESP_MAX);
        c.ticket = 0;
        mesh_rt_fence_acq_rel();
        if (mesh_rt_ld_acquire(e + MESH_PT_GEN) == g) return c;
    }
}

/* ---- ids */
MESH_RT_INLINE uint32_t mesh_ctx_slot(const mesh_ctx *c) { return c->slot; }
/* This node's number (its block), 0..nodes. */
MESH_RT_INLINE uint32_t mesh_ctx_node(const mesh_ctx *c) { return c->node; }
/* The thread's lane in its warp, 0..32. */
MESH_RT_INLINE uint32_t mesh_ctx_lane(const mesh_ctx *c) { return c->lane; }
MESH_RT_INLINE uint32_t mesh_ctx_nodes(const mesh_ctx *c) { return c->nodes; }
/* The thread's warp in the node, 0..warps. */
MESH_RT_INLINE uint32_t mesh_ctx_warp(const mesh_ctx *c) { return c->warp; }
/* The node's warps (the manifest's `warps`). */
MESH_RT_INLINE uint32_t mesh_ctx_warps(const mesh_ctx *c) { return c->warps; }
/* The thread's index in the node, 0..threads (= 32 x warp + lane). */
MESH_RT_INLINE uint32_t mesh_ctx_thread(const mesh_ctx *c) { return c->thread; }
MESH_RT_INLINE uint32_t mesh_ctx_thread_id(const mesh_ctx *c) { return c->thread; }
/* Threads per node (32 x warps). */
MESH_RT_INLINE uint32_t mesh_ctx_threads(const mesh_ctx *c) { return c->warps * 32u; }
/* Thread 0 of the node: the one that makes side effects. */
MESH_RT_INLINE bool mesh_ctx_leader(const mesh_ctx *c) { return c->thread == 0; }

/* ---- the block (every thread of the node calls these; @ajax/mesh-rt-block) */
MESH_RT_INLINE void mesh_ctx_block_barrier(const mesh_ctx *c) { (void)c; mesh_rt_bar_sync(); }
MESH_RT_INLINE uint32_t mesh_ctx_block_broadcast_u32(const mesh_ctx *c, uint32_t v, uint32_t src) { (void)c; return mesh_block_broadcast_u32(v, src); }
MESH_RT_INLINE float mesh_ctx_block_broadcast_f32(const mesh_ctx *c, float v, uint32_t src) { (void)c; return mesh_block_broadcast_f32(v, src); }
MESH_RT_INLINE uint64_t mesh_ctx_block_broadcast_u64(const mesh_ctx *c, uint64_t v, uint32_t src) { (void)c; return mesh_block_broadcast_u64(v, src); }
MESH_RT_INLINE uint32_t mesh_ctx_block_reduce_u32(const mesh_ctx *c, uint32_t v, uint32_t id, mesh_op_u32 op) { (void)c; return mesh_block_reduce_u32(v, id, op); }
MESH_RT_INLINE float mesh_ctx_block_reduce_f32(const mesh_ctx *c, float v, float id, mesh_op_f32 op) { (void)c; return mesh_block_reduce_f32(v, id, op); }
MESH_RT_INLINE uint64_t mesh_ctx_block_reduce_u64(const mesh_ctx *c, uint64_t v, uint64_t id, mesh_op_u64 op) { (void)c; return mesh_block_reduce_u64(v, id, op); }
MESH_RT_INLINE uint32_t mesh_ctx_block_scan_u32_op(const mesh_ctx *c, uint32_t v, uint32_t id, mesh_op_u32 op, bool inclusive) { (void)c; return mesh_block_scan_u32(v, id, op, inclusive); }
MESH_RT_INLINE float mesh_ctx_block_scan_f32_op(const mesh_ctx *c, float v, float id, mesh_op_f32 op, bool inclusive) { (void)c; return mesh_block_scan_f32(v, id, op, inclusive); }
MESH_RT_INLINE uint64_t mesh_ctx_block_scan_u64_op(const mesh_ctx *c, uint64_t v, uint64_t id, mesh_op_u64 op, bool inclusive) { (void)c; return mesh_block_scan_u64(v, id, op, inclusive); }
MESH_RT_INLINE float mesh_ctx_block_sum_f32(const mesh_ctx *c, float v) { (void)c; return mesh_block_reduce_f32(v, MESH_SUM_F32_ID, mesh_add_f32); }
MESH_RT_INLINE float mesh_ctx_block_max_f32(const mesh_ctx *c, float v) {
    (void)c;
    return mesh_block_reduce_f32(v, mesh_f32_from_bits(MESH_MAX_F32_ID_BITS), mesh_max_f32);
}
MESH_RT_INLINE uint32_t mesh_ctx_block_sum_u32(const mesh_ctx *c, uint32_t v) { (void)c; return mesh_block_reduce_u32(v, 0, mesh_add_u32); }
MESH_RT_INLINE uint32_t mesh_ctx_block_max_u32(const mesh_ctx *c, uint32_t v) { (void)c; return mesh_block_reduce_u32(v, 0, mesh_max_u32); }
MESH_RT_INLINE uint64_t mesh_ctx_block_sum_u64(const mesh_ctx *c, uint64_t v) { (void)c; return mesh_block_reduce_u64(v, 0, mesh_add_u64); }
MESH_RT_INLINE uint64_t mesh_ctx_block_max_u64(const mesh_ctx *c, uint64_t v) { (void)c; return mesh_block_reduce_u64(v, 0, mesh_max_u64); }
/* Inclusive / exclusive prefix sums over the node's threads in thread order. */
MESH_RT_INLINE float mesh_ctx_block_scan_f32(const mesh_ctx *c, float v, bool inclusive) { (void)c; return mesh_block_scan_f32(v, MESH_SUM_F32_ID, mesh_add_f32, inclusive); }
MESH_RT_INLINE uint32_t mesh_ctx_block_scan_u32(const mesh_ctx *c, uint32_t v, bool inclusive) { (void)c; return mesh_block_scan_u32(v, 0, mesh_add_u32, inclusive); }
MESH_RT_INLINE uint64_t mesh_ctx_block_scan_u64(const mesh_ctx *c, uint64_t v, bool inclusive) { (void)c; return mesh_block_scan_u64(v, 0, mesh_add_u64, inclusive); }
/* A block-strided loop: i = thread(), thread() + threads(), .. < n (Rust's for_strided). */
#define MESH_CTX_FOR_STRIDED(c, n, i) for (uint32_t i = (c)->thread, i##_end_ = (n); i < i##_end_; i += mesh_ctx_threads(c))

/* The program's scratch: device memory of its own (manifest `scratch_bytes`,
 * zeroed at load, kept while the image is loaded, never seen by the host),
 * bounds checked like the view. Empty without one. */
MESH_RT_INLINE mesh_view mesh_ctx_scratch_view(const mesh_ctx *c) {
    return (mesh_view){c->scratch, c->scratch ? c->scratch_len : 0, c->ctl};
}
/* Its bytes (the manifest's `scratch_bytes`; 0 without one). */
MESH_RT_INLINE uint32_t mesh_ctx_scratch_bytes(const mesh_ctx *c) { return c->scratch ? c->scratch_len : 0; }
/* Its raw base (unchecked access: a kernel's inner loops; plain loads, cached). */
MESH_RT_INLINE uint8_t *mesh_ctx_scratch_ptr(const mesh_ctx *c, uint32_t *len) {
    *len = mesh_ctx_scratch_bytes(c);
    return c->scratch;
}
/* The program's shared memory bytes per node (the manifest's `shared_bytes`). */
MESH_RT_INLINE uint32_t mesh_ctx_shared_len(const mesh_ctx *c) { return c->smem_len; }
/* len elements of esize bytes (alignment esize) at byte off of the program's
 * shared memory: the same memory for every thread of the node. False (and
 * FAULT_SHARED_BOUNDS | off << 8) when outside `shared_bytes` or misaligned. */
MESH_RT_INLINE bool mesh_ctx_shared(const mesh_ctx *c, uint32_t off, uint32_t len, uint32_t esize, mesh_shared *out) {
    bool ok = off % esize == 0 && (uint64_t)off + (uint64_t)esize * len <= c->smem_len;
    if (!ok) { mesh_rt_fault(c->ctl, MESH_FAULT_SHARED_BOUNDS | off << 8); return false; }
    *out = (mesh_shared){c->smem + off, len, c->ctl};
    return true;
}
/* The running task's ring ticket (the same on every lane). Tickets of one
 * publish are consecutive, so a batch's task with arg index k has ticket t0 + k. */
MESH_RT_INLINE uint32_t mesh_ctx_ticket(const mesh_ctx *c) { return c->ticket; }
/* This system's number in the cluster (0..systems()), or PEER_NONE. */
MESH_RT_INLINE uint32_t mesh_ctx_system(const mesh_ctx *c) { return mesh_rt_ld_relaxed(c->mb + MESH_H_PEER); }
/* The cluster's size N (H_SYSTEMS; 1 single). */
MESH_RT_INLINE uint32_t mesh_ctx_systems(const mesh_ctx *c) { return mesh_rt_ld_relaxed(c->mb + MESH_H_SYSTEMS); }
/* The lockstep group this member is in: its rank r and the group's size N,
 * with 2 <= N <= MAX_SYSTEMS and r < N; false outside a group. */
MESH_RT_INLINE bool mesh_ctx_group(const mesh_ctx *c, uint32_t *rank, uint32_t *n) {
    uint32_t r = mesh_rt_ld_relaxed(c->mb + MESH_H_GROUP_RANK), k = mesh_rt_ld_relaxed(c->mb + MESH_H_GROUP_N);
    if (!(k >= 2 && k <= MESH_MAX_SYSTEMS && r < k)) return false;
    *rank = r;
    *n = k;
    return true;
}
/* The manifest's `param` (PX_PARAM; 0 when the manifest has none). */
MESH_RT_INLINE uint32_t mesh_ctx_param(const mesh_ctx *c) { return mesh_rt_ld_relaxed(c->mb + mesh_prog_ext(c->slot) + MESH_PX_PARAM); }
/* The device's SM count (H_SMS). */
MESH_RT_INLINE uint32_t mesh_ctx_sms(const mesh_ctx *c) { return mesh_rt_ld_relaxed(c->mb + MESH_H_SMS); }

/* ---- own view */
MESH_RT_INLINE mesh_view mesh_ctx_view(const mesh_ctx *c) { return c->view; }
MESH_RT_INLINE uint32_t mesh_ctx_view_len(const mesh_ctx *c) { return c->view.len; }
MESH_RT_INLINE uint32_t mesh_ctx_view_u32(const mesh_ctx *c, uint32_t off) { return mesh_view_u32(c->view, off); }
MESH_RT_INLINE float mesh_ctx_view_f32(const mesh_ctx *c, uint32_t off) { return mesh_view_f32(c->view, off); }
MESH_RT_INLINE uint64_t mesh_ctx_view_u64(const mesh_ctx *c, uint32_t off) { return mesh_view_u64(c->view, off); }
MESH_RT_INLINE uint8_t mesh_ctx_view_u8(const mesh_ctx *c, uint32_t off) { return mesh_view_u8(c->view, off); }
MESH_RT_INLINE void mesh_ctx_set_u32(const mesh_ctx *c, uint32_t off, uint32_t v) { mesh_view_set_u32(c->view, off, v); }
MESH_RT_INLINE void mesh_ctx_set_f32(const mesh_ctx *c, uint32_t off, float v) { mesh_view_set_f32(c->view, off, v); }
MESH_RT_INLINE void mesh_ctx_set_u64(const mesh_ctx *c, uint32_t off, uint64_t v) { mesh_view_set_u64(c->view, off, v); }
MESH_RT_INLINE void mesh_ctx_set_u8(const mesh_ctx *c, uint32_t off, uint8_t v) { mesh_view_set_u8(c->view, off, v); }

/* ---- other programs */
/* Slot s's view, if the manifest's `reads` lists it and the slot holds a
 * program; false otherwise (an undeclared read also records FAULT_PEER_VIEW).
 * Writes through it are the caller's responsibility (`reads` is advisory). */
MESH_RT_INLINE bool mesh_ctx_peer_view(const mesh_ctx *c, uint32_t s, mesh_view *out) {
    if (s >= MESH_PROG_SLOTS) return false;
    if ((c->reads_mask & (1u << s)) == 0) { mesh_rt_fault(c->ctl, MESH_FAULT_PEER_VIEW | s << 8); return false; }
    /* Slot s's entry may be rewritten while we run (its rollback, its unload):
     * offset, length and state are read as one, under its seqlock. */
    uint8_t *e = c->mb + mesh_prog_entry(s);
    for (;;) {
        uint32_t g = mesh_rt_ld_acquire(e + MESH_PT_GEN);
        if (g & 1u) { mesh_rt_nanosleep(1000); continue; }
        uint32_t st = mesh_rt_ld_relaxed(e + MESH_PT_STATE), off = mesh_rt_rd(e + MESH_PT_VIEW_OFF), len = mesh_rt_rd(e + MESH_PT_VIEW_BYTES);
        mesh_rt_fence_acq_rel();
        if (mesh_rt_ld_acquire(e + MESH_PT_GEN) != g) continue;
        if (st == MESH_ST_EMPTY || st == MESH_ST_FAILED) return false;
        *out = (mesh_view){c->mb + off, len, c->ctl};
        return true;
    }
}
MESH_RT_INLINE uint32_t mesh_ctx_prog_state(const mesh_ctx *c, uint32_t s) { return mesh_rt_ld_relaxed(c->mb + mesh_prog_entry(s) + MESH_PT_STATE); }
/* Slot s's name, NUL padded. */
MESH_RT_INLINE void mesh_ctx_prog_name(const mesh_ctx *c, uint32_t s, uint8_t out[MESH_NAME_BYTES]) {
    for (uint32_t i = 0; i < MESH_NAME_BYTES; i++) out[i] = *(const volatile uint8_t *)(c->mb + mesh_prog_entry(s) + MESH_PT_NAME + i);
}
MESH_RT_INLINE bool mesh_ctx_prog_name_is(const mesh_ctx *c, uint32_t s, const char *name, uint32_t len) {
    uint8_t n[MESH_NAME_BYTES];
    mesh_ctx_prog_name(c, s, n);
    if (len > MESH_NAME_MAX) return false;
    for (uint32_t i = 0; i < len; i++)
        if (n[i] != (uint8_t)name[i]) return false;
    return n[len] == 0;
}
/* The slot holding the program `name` (len bytes), if any (the table is public). */
MESH_RT_INLINE bool mesh_ctx_find(const mesh_ctx *c, const char *name, uint32_t len, uint32_t *slot) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++)
        if (mesh_ctx_prog_state(c, s) != MESH_ST_EMPTY && mesh_ctx_prog_name_is(c, s, name, len)) { *slot = s; return true; }
    return false;
}
/* Asks meshd to start/stop slot s: advances the GPU-owned P_RUN_REQ
 * (n << 1 | on) with atom.cas; meshd only reads it. Only for a program with
 * reads = ["*"] (http). Call from one lane. */
MESH_RT_INLINE bool mesh_ctx_request_run(const mesh_ctx *c, uint32_t s, bool on) {
    if (c->reads_mask != MESH_READS_ALL || s >= MESH_PROG_SLOTS) return false;
    uint8_t *w = c->mb + mesh_prog_ctl(s) + MESH_P_RUN_REQ;
    mesh_rt_fence_acq_rel();
    uint32_t cur = mesh_rt_ld_relaxed(w);
    for (;;) {
        uint32_t seen = mesh_rt_atom_cas(w, cur, mesh_run_req_next(cur, on));
        if (seen == cur) break;
        cur = seen;
    }
    return true;
}
/* The whole mailbox, for a READS_ALL program (http renders the table); NULL otherwise. */
MESH_RT_INLINE uint8_t *mesh_ctx_mailbox(const mesh_ctx *c) { return c->reads_mask == MESH_READS_ALL ? c->mb : NULL; }

/* ---- input queue (manifest `input`) */
/* Bytes waiting in the input queue. */
MESH_RT_INLINE uint32_t mesh_ctx_input_pending(const mesh_ctx *c) {
    if (c->input_len == 0) return 0;
    uint8_t *q = c->view.base + c->input_off;
    return mesh_rt_ld_acquire(q + MESH_IQ_HEAD) - mesh_rt_rd(q + MESH_IQ_TAIL);
}
/* Collective: the next input byte, the same on every lane; false when empty. */
MESH_RT_INLINE bool mesh_ctx_input_pop(const mesh_ctx *c, uint8_t *out) {
    uint32_t v = 0x100;
    if (c->lane == 0 && c->input_len != 0) {
        uint8_t *q = c->view.base + c->input_off;
        uint32_t tail = mesh_rt_rd(q + MESH_IQ_TAIL);
        if (mesh_rt_ld_acquire(q + MESH_IQ_HEAD) != tail) {
            v = *(const volatile uint8_t *)(q + MESH_IQ_DATA + (tail & (c->input_len - 1)));
            mesh_rt_st_release(q + MESH_IQ_TAIL, tail + 1);
        }
    }
    v = mesh_rt_shfl_idx(v, 0);
    if (v >= 0x100) return false;
    *out = (uint8_t)v;
    return true;
}

/* ---- pipes (manifest `inbox`, `feeds`; @ajax/mesh-abi-pipe) */
typedef void (*mesh_ctx_record_fn)(mesh_view rec, void *user);

/* Collective: pushes one record into slot s's inbox. Lane 0 claims a cell;
 * then every lane calls fill with a view of the cell (the consumer's record
 * size: lane l typically writes words l, l + 32, ...), and lane 0 publishes
 * it. PIPE_OK, PIPE_NONE (full: keep the record, try later), or a negative
 * code (not fed / gone; not fed also records FAULT_PIPE). The same on every lane. */
MESH_RT_INLINE int32_t mesh_ctx_push_with(const mesh_ctx *c, uint32_t s, mesh_ctx_record_fn fill, void *user) {
    mesh_words w = mesh_rt_gw(c->mb);
    uint32_t code = (uint32_t)MESH_PIPE_NONE, pos = 0, base = 0, rec = 0, parity = 0;
    bool have = false;
    mesh_pipe_ring ring = {0, 0, 0};
    if (c->lane == 0) {
        /* Counted in flight (flight_word) from before the block is read until after the commit. */
        parity = mesh_pipe_flight_begin(&w, c->slot);
        mesh_pipe_inbox in;
        int r = mesh_pipe_target(&w, c->slot, s, &in);
        if (r < 0) {
            if (r == -MESH_PIPE_ERR_NOT_CONNECTED) mesh_rt_fault(c->ctl, MESH_FAULT_PIPE | s << 8);
            code = (uint32_t)mesh_pipe_code(r);
        } else {
            /* A GPU producer pushes on the inbox's GPU side (its head is CASed by GPU warps only). */
            mesh_pipe_ring rr = mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_GPU);
            uint32_t p;
            if (mesh_pipe_ring_reserve_push(&rr, &w, c->slot, &p)) {
                code = (uint32_t)MESH_PIPE_OK;
                pos = p;
                base = (uint32_t)mesh_pipe_ring_cell(&rr, p);
                rec = rr.rec;
                ring = rr;
                have = true;
            }
        }
        if (!have) mesh_pipe_flight_end(&w, c->slot, parity);
    }
    int32_t cd = (int32_t)mesh_rt_shfl_idx(code, 0);
    if (cd != MESH_PIPE_OK) return cd;
    base = mesh_rt_shfl_idx(base, 0);
    rec = mesh_rt_shfl_idx(rec, 0);
    fill((mesh_view){c->mb + base, rec, c->ctl}, user);
    /* Every lane's record words before lane 0's release of the cell. */
    mesh_rt_fence_acq_rel();
    mesh_rt_warp_sync();
    /* The record's sum (pipe rec_sum), lanes summing words l, l + 32, ... */
    uint32_t part = 0;
    for (uint32_t k = c->lane; k < rec / 4; k += 32) part += mesh_pipe_sum_part(k, mesh_rt_rd(c->mb + base + 4 * k));
    for (uint32_t m = 16; m > 0; m >>= 1) part += mesh_rt_shfl_bfly(part, m);
    if (have) {
        mesh_rt_st_relaxed(c->mb + mesh_pipe_ring_sum(&ring, pos), part ^ mesh_pipe_pos_mix(pos));
        mesh_pipe_ring_commit_push(&ring, &w, pos);
        mesh_pipe_flight_end(&w, c->slot, parity);
    }
    mesh_rt_warp_sync();
    return cd;
}
/* Collective: pops the own inbox's next record. Every lane calls read with a
 * view of it; then lane 0 frees the cell. PIPE_OK, PIPE_NONE (empty), or a
 * negative code (no inbox). The same on every lane. */
MESH_RT_INLINE int32_t mesh_ctx_pop_with(const mesh_ctx *c, mesh_ctx_record_fn read, void *user) {
    mesh_words w = mesh_rt_gw(c->mb);
    uint32_t code = (uint32_t)MESH_PIPE_NONE, pos = 0, base = 0, rec = 0;
    bool have = false;
    mesh_pipe_ring ring = {0, 0, 0};
    if (c->lane == 0) {
        mesh_pipe_inbox in;
        int r = mesh_pipe_target(&w, c->slot, c->slot, &in);
        if (r < 0) {
            code = (uint32_t)mesh_pipe_code(r);
        } else {
            /* Both sides, the fuller first. */
            uint32_t k = mesh_pipe_inbox_first(&in, &w);
            uint32_t sides[2] = {k, k ^ 1u};
            for (int i = 0; i < 2; i++) {
                mesh_pipe_ring rr = mesh_pipe_inbox_side(&in, sides[i]);
                uint32_t t;
                if (mesh_pipe_ring_reserve_pop(&rr, &w, &t)) {
                    code = (uint32_t)MESH_PIPE_OK;
                    pos = t;
                    base = (uint32_t)mesh_pipe_ring_cell(&rr, t);
                    rec = rr.rec;
                    ring = rr;
                    have = true;
                    break;
                }
            }
            if (!have) mesh_rt_red_add(c->mb + mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_CPU).base + MESH_PIPE_PI_EMPTY, 1);
        }
    }
    int32_t cd = (int32_t)mesh_rt_shfl_idx(code, 0);
    if (cd != MESH_PIPE_OK) return cd;
    base = mesh_rt_shfl_idx(base, 0);
    rec = mesh_rt_shfl_idx(rec, 0);
    /* Lane 0 acquired the cell's lap word: order every lane's reads after it. */
    mesh_rt_fence_acq_rel();
    read((mesh_view){c->mb + base, rec, c->ctl}, user);
    mesh_rt_warp_sync();
    if (have) mesh_pipe_ring_commit_pop(&ring, &w, pos);
    mesh_rt_warp_sync();
    return cd;
}
/* Collective: free cells in slot s's inbox (one this program feeds), as lane
 * 0 read them (the same on every lane); false when it may not push there. */
MESH_RT_INLINE bool mesh_ctx_pipe_free(const mesh_ctx *c, uint32_t s, uint32_t *out) {
    uint32_t v = 0;
    if (c->lane == 0) {
        mesh_words w = mesh_rt_gw(c->mb);
        mesh_pipe_inbox in;
        if (mesh_pipe_target(&w, c->slot, s, &in) < 0) v = UINT32_MAX;
        else {
            mesh_pipe_ring rr = mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_GPU);
            v = mesh_pipe_ring_free(&rr, &w);
        }
    }
    v = mesh_rt_shfl_idx(v, 0);
    if (v == UINT32_MAX) return false;
    *out = v;
    return true;
}
/* Collective: records waiting in slot s's inbox (the own slot, or one this
 * program feeds), as lane 0 read them; false otherwise. */
MESH_RT_INLINE bool mesh_ctx_pipe_len(const mesh_ctx *c, uint32_t s, uint32_t *out) {
    uint32_t v = 0;
    if (c->lane == 0) {
        mesh_words w = mesh_rt_gw(c->mb);
        mesh_pipe_inbox in;
        v = mesh_pipe_target(&w, c->slot, s, &in) < 0 ? UINT32_MAX : mesh_pipe_inbox_len(&in, &w);
    }
    v = mesh_rt_shfl_idx(v, 0);
    if (v == UINT32_MAX) return false;
    *out = v;
    return true;
}

/* ---- request window (manifest `request`, op OP_REQ) */
/* The request's bytes (P_REQ_LEN, clamped to req_max). */
MESH_RT_INLINE mesh_view mesh_ctx_request(const mesh_ctx *c) {
    uint32_t len = mesh_rt_ld_relaxed(c->ctl + MESH_P_REQ_LEN);
    if (len > c->req_max) len = c->req_max;
    return (mesh_view){c->view.base + c->req_off, len, c->ctl};
}
/* The response window (resp_max bytes). */
MESH_RT_INLINE mesh_view mesh_ctx_response(const mesh_ctx *c) { return (mesh_view){c->view.base + c->resp_off, c->resp_max, c->ctl}; }
/* Collective: publishes len response bytes (P_RESP_LEN, then P_RESP_SEQ = P_REQ_SEQ, release). */
MESH_RT_INLINE void mesh_ctx_respond(const mesh_ctx *c, uint32_t len) {
    mesh_rt_warp_sync();
    if (c->lane == 0) {
        if (len > c->resp_max) {
            mesh_rt_fault(c->ctl, MESH_FAULT_RESP_BOUNDS | len << 8);
            len = c->resp_max;
        }
        mesh_rt_wr(c->ctl + MESH_P_RESP_LEN, len);
        mesh_rt_st_release64(c->ctl + MESH_P_RESP_SEQ, mesh_rt_ld_acquire64(c->ctl + MESH_P_REQ_SEQ));
    }
    mesh_rt_warp_sync();
}

/* ---- lockstep */
/* The exchange page, only for the program with peer = "lockstep". */
MESH_RT_INLINE bool mesh_ctx_exchange(const mesh_ctx *c, mesh_exchange *out) {
    if (c->peer_mode != MESH_PEER_MODE_LOCKSTEP) return false;
    out->base = c->mb + MESH_EX_BASE;
    return true;
}

/* ---- counters (manifest `counters`) */
/* Adds n to generic counter i (0..4). Call from one lane. */
MESH_RT_INLINE void mesh_ctx_count(const mesh_ctx *c, uint32_t i, uint32_t n) {
    if (i < MESH_COUNTERS) mesh_rt_red_add(c->ctl + MESH_P_COUNTERS + 4 * i, n);
}
MESH_RT_INLINE uint32_t mesh_ctx_counter(const mesh_ctx *c, uint32_t i) {
    return i < MESH_COUNTERS ? mesh_rt_ld_relaxed(c->ctl + MESH_P_COUNTERS + 4 * i) : 0;
}

#endif
