/* @ajax/mesh-rt-wasm: the runtime a CPU mesh program is built on -- mesh-rt's surface for meshd's
 * wasm executor (mesh-abi BACKEND_WASM), ported from alpha-rust-mesh crates/mesh-rt-wasm. A program
 * (`programs/<name>-wasm`, a sexc "wasm" package) includes this and writes
 *
 *   static void init(const mesh_ctx *ctx) { ... }                                  // op 1 (OP_INIT)
 *   static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) { ... }    // op 0 and 2..
 *   MESH_PROGRAM(init, task)
 *
 * which exports mesh_init, mesh_task and mesh_rt_abi (the contract is mesh-abi v2::wasm,
 * @ajax/mesh-abi-wasm). Differences from mesh-rt, all from running on one CPU thread instead of a
 * warp:
 * - a node is a lane thread of meshd running one instance: mesh_ctx_lane() is 0,
 *   mesh_ctx_leader() true, and the collectives (input_pop, respond) are plain calls;
 * - the mailbox is not in linear memory: a mesh_view names a window and every access is a
 *   bounds-checked mesh::* import (out of range: 0 / dropped and P_FAULT, as on the GPU). Bulk
 *   read/write move whole ranges in one call -- what a hot loop should use;
 * - linear memory is scratch, like registers: state that must survive a task, a restart or an
 *   upgrade lives in the view;
 * - f32 in the cluster float mode is Z (@ajax/mesh-ftz: on wasm32 its operations are the exported
 *   mesh_ftz_* helpers): an image whose manifest declares float = "ftz" (the default) is refused by
 *   meshd if any f32 arithmetic bypasses it. */
#ifndef MESH_RT_WASM_H
#define MESH_RT_WASM_H

#include "mesh/abi/base.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/wasm.h"
#include "mesh/ftz.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the mesh::* imports ---- */

#define MESH_IMPORT(nm) __attribute__((import_module("mesh"), import_name(#nm)))
MESH_IMPORT(info) uint32_t mesh_sys_info(uint32_t code);
MESH_IMPORT(ld8) uint32_t mesh_sys_ld8(uint32_t h, uint32_t off);
MESH_IMPORT(ld32) uint32_t mesh_sys_ld32(uint32_t h, uint32_t off);
MESH_IMPORT(ld64) uint64_t mesh_sys_ld64(uint32_t h, uint32_t off);
MESH_IMPORT(st8) void mesh_sys_st8(uint32_t h, uint32_t off, uint32_t v);
MESH_IMPORT(st32) void mesh_sys_st32(uint32_t h, uint32_t off, uint32_t v);
MESH_IMPORT(st64) void mesh_sys_st64(uint32_t h, uint32_t off, uint64_t v);
MESH_IMPORT(cas32) uint32_t mesh_sys_cas32(uint32_t h, uint32_t off, uint32_t cmp, uint32_t desired);
MESH_IMPORT(add32) uint32_t mesh_sys_add32(uint32_t h, uint32_t off, uint32_t v);
MESH_IMPORT(add64) uint64_t mesh_sys_add64(uint32_t h, uint32_t off, uint64_t v);
MESH_IMPORT(read) void mesh_sys_read(uint32_t h, uint32_t off, void *ptr, uint32_t len);
MESH_IMPORT(write) void mesh_sys_write(uint32_t h, uint32_t off, const void *ptr, uint32_t len);
MESH_IMPORT(peer) int32_t mesh_sys_peer(uint32_t slot);
MESH_IMPORT(find) int32_t mesh_sys_find(const void *ptr, uint32_t len);
MESH_IMPORT(prog_state) uint32_t mesh_sys_prog_state(uint32_t slot);
MESH_IMPORT(count) void mesh_sys_count(uint32_t i, uint32_t n);
MESH_IMPORT(counter) uint32_t mesh_sys_counter(uint32_t i);
MESH_IMPORT(input_pending) uint32_t mesh_sys_input_pending(void);
MESH_IMPORT(input_pop) int32_t mesh_sys_input_pop(void);
MESH_IMPORT(respond) void mesh_sys_respond(uint32_t len);
MESH_IMPORT(exchange) uint32_t mesh_sys_exchange(uint64_t n);
MESH_IMPORT(pipe_push) int32_t mesh_sys_pipe_push(uint32_t slot, const void *ptr, uint32_t len);
MESH_IMPORT(pipe_pop) int32_t mesh_sys_pipe_pop(void *ptr, uint32_t len);
MESH_IMPORT(pipe_len) int32_t mesh_sys_pipe_len(uint32_t slot);
MESH_IMPORT(host_ns) uint64_t mesh_sys_host_ns(void);
MESH_IMPORT(fault) void mesh_sys_fault(uint32_t code);
#undef MESH_IMPORT

/* ---- View: a byte window of the mailbox (a view or a part of one). Offsets are bytes from its
 * start; u32/f32 accessors need 4-aligned offsets, u64 8-aligned. ---- */

typedef struct {
    uint32_t h, base, len;
} mesh_view;

static inline bool mesh_view_ok(mesh_view v, uint32_t off, uint32_t n) {
    bool fine = (uint64_t)off + n <= v.len;
    if (!fine) mesh_sys_fault(MESH_FAULT_VIEW_BOUNDS | off << 8);
    return fine;
}
static inline uint32_t mesh_view_len(mesh_view v) { return v.len; }
static inline bool mesh_view_is_empty(mesh_view v) { return v.len == 0; }
static inline uint32_t mesh_view_u32(mesh_view v, uint32_t off) { return mesh_view_ok(v, off, 4) ? mesh_sys_ld32(v.h, v.base + off) : 0; }
static inline float mesh_view_f32(mesh_view v, uint32_t off) { return mesh_ftz_from_bits(mesh_view_u32(v, off)); }
static inline uint64_t mesh_view_u64(mesh_view v, uint32_t off) { return mesh_view_ok(v, off, 8) ? mesh_sys_ld64(v.h, v.base + off) : 0; }
static inline uint8_t mesh_view_u8(mesh_view v, uint32_t off) { return mesh_view_ok(v, off, 1) ? (uint8_t)mesh_sys_ld8(v.h, v.base + off) : 0; }
static inline void mesh_view_set_u32(mesh_view v, uint32_t off, uint32_t x) { if (mesh_view_ok(v, off, 4)) mesh_sys_st32(v.h, v.base + off, x); }
static inline void mesh_view_set_f32(mesh_view v, uint32_t off, float x) { mesh_view_set_u32(v, off, mesh_ftz_bits(x)); }
static inline void mesh_view_set_u64(mesh_view v, uint32_t off, uint64_t x) { if (mesh_view_ok(v, off, 8)) mesh_sys_st64(v.h, v.base + off, x); }
static inline void mesh_view_set_u8(mesh_view v, uint32_t off, uint8_t x) { if (mesh_view_ok(v, off, 1)) mesh_sys_st8(v.h, v.base + off, x); }
/* Atomics (acq_rel): compare-and-swap (the value seen), fetch-adds (the old value). */
static inline uint32_t mesh_view_cas32(mesh_view v, uint32_t off, uint32_t cmp, uint32_t desired) {
    return mesh_view_ok(v, off, 4) ? mesh_sys_cas32(v.h, v.base + off, cmp, desired) : 0;
}
static inline uint32_t mesh_view_add32(mesh_view v, uint32_t off, uint32_t x) { return mesh_view_ok(v, off, 4) ? mesh_sys_add32(v.h, v.base + off, x) : 0; }
static inline uint64_t mesh_view_add64(mesh_view v, uint32_t off, uint64_t x) { return mesh_view_ok(v, off, 8) ? mesh_sys_add64(v.h, v.base + off, x) : 0; }
/* n bytes from off into dst in one call, and back. */
static inline void mesh_view_read(mesh_view v, uint32_t off, void *dst, uint32_t n) { if (mesh_view_ok(v, off, n)) mesh_sys_read(v.h, v.base + off, dst, n); }
static inline void mesh_view_write(mesh_view v, uint32_t off, const void *src, uint32_t n) { if (mesh_view_ok(v, off, n)) mesh_sys_write(v.h, v.base + off, src, n); }
/* u32 / f32 words (little-endian, as the view holds them). */
static inline void mesh_view_read_u32s(mesh_view v, uint32_t off, uint32_t *dst, uint32_t n) { mesh_view_read(v, off, dst, 4 * n); }
static inline void mesh_view_write_u32s(mesh_view v, uint32_t off, const uint32_t *src, uint32_t n) { mesh_view_write(v, off, src, 4 * n); }
static inline void mesh_view_read_f32s(mesh_view v, uint32_t off, float *dst, uint32_t n) { mesh_view_read(v, off, dst, 4 * n); }
static inline void mesh_view_write_f32s(mesh_view v, uint32_t off, const float *src, uint32_t n) { mesh_view_write(v, off, src, 4 * n); }
/* A sub-window [off, off + len): true with *out, or false (and a fault) when out of range. */
static inline bool mesh_view_sub(mesh_view v, uint32_t off, uint32_t len, mesh_view *out) {
    if (!mesh_view_ok(v, off, len)) return false;
    out->h = v.h;
    out->base = v.base + off;
    out->len = len;
    return true;
}

/* ---- Exchange: the exchange page (EX_BASE) for the lockstep program: system k's u64 round
 * counter and its two EX_SLOT_BYTES slots (parity 0, 1). ---- */

typedef struct {
    mesh_view w;
} mesh_exchange;

#define MESH_EX_SLOT_WORDS (MESH_EX_SLOT_BYTES / 4)

static inline uint64_t mesh_exchange_counter(mesh_exchange x, uint32_t k) {
    return mesh_view_u64(x.w, (uint32_t)(mesh_ex_counter(k) - MESH_EX_BASE));
}
/* System k's slot of parity par (0, 1) as u32 words. */
static inline void mesh_exchange_slot(mesh_exchange x, uint32_t k, uint32_t par, uint32_t out[MESH_EX_SLOT_WORDS]) {
    mesh_view_read_u32s(x.w, (uint32_t)(mesh_ex_slots(k) - MESH_EX_BASE) + MESH_EX_SLOT_BYTES * par, out, MESH_EX_SLOT_WORDS);
}
static inline void mesh_exchange_set_slot(mesh_exchange x, uint32_t k, uint32_t par, const uint32_t w[MESH_EX_SLOT_WORDS]) {
    mesh_view_write_u32s(x.w, (uint32_t)(mesh_ex_slots(k) - MESH_EX_BASE) + MESH_EX_SLOT_BYTES * par, w, MESH_EX_SLOT_WORDS);
}
/* Round n: publish this system's counter = n (once), wait -- bounded, PEER_POLLS x PEER_POLL_NS,
 * in meshd -- for every member's. true: all are there. */
static inline bool mesh_exchange_round(mesh_exchange x, uint64_t n) {
    (void)x;
    return mesh_sys_exchange(n) == 1;
}

/* ---- Ctx: what a program's init/task see. ---- */

typedef struct {
    uint32_t slot, node, nodes;
    mesh_view view;
    uint32_t req_off, resp_off, resp_max;
    bool lockstep;
} mesh_ctx;

/* The instance's Ctx (built on first use; its facts are fixed for the instance's life). */
const mesh_ctx *mesh_ctx_get(void);

/* ids */
static inline uint32_t mesh_ctx_slot(const mesh_ctx *c) { return c->slot; }
static inline uint32_t mesh_ctx_node(const mesh_ctx *c) { return c->node; }
static inline uint32_t mesh_ctx_lane(const mesh_ctx *c) { (void)c; return 0; }
static inline uint32_t mesh_ctx_nodes(const mesh_ctx *c) { return c->nodes; }
static inline bool mesh_ctx_leader(const mesh_ctx *c) { (void)c; return true; }
/* The running task's ring ticket. */
static inline uint32_t mesh_ctx_ticket(const mesh_ctx *c) { (void)c; return mesh_sys_info(MESH_WASM_I_TICKET); }
/* This system's number in the cluster (0..systems), or PEER_NONE. */
static inline uint32_t mesh_ctx_system(const mesh_ctx *c) { (void)c; return mesh_sys_info(MESH_WASM_I_SYSTEM); }
static inline uint32_t mesh_ctx_systems(const mesh_ctx *c) { (void)c; return mesh_sys_info(MESH_WASM_I_SYSTEMS); }
/* The lockstep group this member is in: true with (rank, size), 2 <= size <= MAX_SYSTEMS, rank < size. */
static inline bool mesh_ctx_group(const mesh_ctx *c, uint32_t *rank, uint32_t *n) {
    (void)c;
    uint32_t r = mesh_sys_info(MESH_WASM_I_GROUP_RANK), k = mesh_sys_info(MESH_WASM_I_GROUP_N);
    if (k < 2 || k > MESH_MAX_SYSTEMS || r >= k) return false;
    *rank = r;
    *n = k;
    return true;
}
/* The manifest's `param` (PX_PARAM, read live). */
static inline uint32_t mesh_ctx_param(const mesh_ctx *c) { (void)c; return mesh_sys_info(MESH_WASM_I_PARAM); }
/* H_SMS (on a CPU-only meshd: its hardware threads). */
static inline uint32_t mesh_ctx_sms(const mesh_ctx *c) { (void)c; return mesh_sys_info(MESH_WASM_I_SMS); }
/* The host clock in ns (the trace's timebase). */
static inline uint64_t mesh_ctx_host_ns(const mesh_ctx *c) { (void)c; return mesh_sys_host_ns(); }

/* own view */
static inline mesh_view mesh_ctx_view(const mesh_ctx *c) { return c->view; }
static inline uint32_t mesh_ctx_view_len(const mesh_ctx *c) { return c->view.len; }
static inline uint32_t mesh_ctx_view_u32(const mesh_ctx *c, uint32_t off) { return mesh_view_u32(c->view, off); }
static inline float mesh_ctx_view_f32(const mesh_ctx *c, uint32_t off) { return mesh_view_f32(c->view, off); }
static inline uint64_t mesh_ctx_view_u64(const mesh_ctx *c, uint32_t off) { return mesh_view_u64(c->view, off); }
static inline uint8_t mesh_ctx_view_u8(const mesh_ctx *c, uint32_t off) { return mesh_view_u8(c->view, off); }
static inline void mesh_ctx_set_u32(const mesh_ctx *c, uint32_t off, uint32_t v) { mesh_view_set_u32(c->view, off, v); }
static inline void mesh_ctx_set_f32(const mesh_ctx *c, uint32_t off, float v) { mesh_view_set_f32(c->view, off, v); }
static inline void mesh_ctx_set_u64(const mesh_ctx *c, uint32_t off, uint64_t v) { mesh_view_set_u64(c->view, off, v); }
static inline void mesh_ctx_set_u8(const mesh_ctx *c, uint32_t off, uint8_t v) { mesh_view_set_u8(c->view, off, v); }

/* other programs: slot s's view for this task, if the manifest's `reads` lists it and the slot
 * holds a program (an undeclared read also records FAULT_PEER_VIEW). */
static inline bool mesh_ctx_peer_view(const mesh_ctx *c, uint32_t s, mesh_view *out) {
    (void)c;
    int32_t len = mesh_sys_peer(s);
    if (len < 0) return false;
    out->h = mesh_wasm_w_peer(s);
    out->base = 0;
    out->len = (uint32_t)len;
    return true;
}
/* The slot holding the program `name` (len bytes): true with *slot. */
static inline bool mesh_ctx_find(const mesh_ctx *c, const char *name, uint32_t len, uint32_t *slot) {
    (void)c;
    int32_t s = mesh_sys_find(name, len);
    if (s < 0) return false;
    *slot = (uint32_t)s;
    return true;
}
static inline uint32_t mesh_ctx_prog_state(const mesh_ctx *c, uint32_t s) { (void)c; return mesh_sys_prog_state(s); }

/* pipes (manifest `inbox`, `feeds`): push one record (exactly the consumer's record size) into slot
 * s's inbox: true pushed, false full (backpressure). push_code gives the full PIPE_* code. */
static inline int32_t mesh_ctx_push_code(const mesh_ctx *c, uint32_t s, const void *rec, uint32_t len) {
    (void)c;
    return mesh_sys_pipe_push(s, rec, len);
}
static inline bool mesh_ctx_push(const mesh_ctx *c, uint32_t s, const void *rec, uint32_t len) {
    return mesh_ctx_push_code(c, s, rec, len) == MESH_PIPE_OK;
}
static inline bool mesh_ctx_push_u32s(const mesh_ctx *c, uint32_t s, const uint32_t *rec, uint32_t n) {
    return mesh_ctx_push(c, s, rec, 4 * n);
}
/* Pops the own inbox's next record into out (its record size): false when empty. */
static inline bool mesh_ctx_pop(const mesh_ctx *c, void *out, uint32_t len) {
    (void)c;
    return mesh_sys_pipe_pop(out, len) == MESH_PIPE_OK;
}
static inline bool mesh_ctx_pop_u32s(const mesh_ctx *c, uint32_t *out, uint32_t n) { return mesh_ctx_pop(c, out, 4 * n); }
/* Records waiting in slot s's inbox (the own slot, or one this program feeds): true with *n. */
static inline bool mesh_ctx_pipe_len(const mesh_ctx *c, uint32_t s, uint32_t *n) {
    (void)c;
    int32_t k = mesh_sys_pipe_len(s);
    if (k < 0) return false;
    *n = (uint32_t)k;
    return true;
}

/* input queue (manifest `input`) */
static inline uint32_t mesh_ctx_input_pending(const mesh_ctx *c) { (void)c; return mesh_sys_input_pending(); }
/* The next input byte: true with *b; false when empty. */
static inline bool mesh_ctx_input_pop(const mesh_ctx *c, uint8_t *b) {
    (void)c;
    int32_t x = mesh_sys_input_pop();
    if (x < 0) return false;
    *b = (uint8_t)x;
    return true;
}

/* request window (manifest `request`, op OP_REQ) */
static inline mesh_view mesh_ctx_request(const mesh_ctx *c) {
    mesh_view v = {MESH_WASM_W_VIEW, c->req_off, mesh_sys_info(MESH_WASM_I_REQ_LEN)};
    return v;
}
static inline mesh_view mesh_ctx_response(const mesh_ctx *c) {
    mesh_view v = {MESH_WASM_W_VIEW, c->resp_off, c->resp_max};
    return v;
}
static inline void mesh_ctx_respond(const mesh_ctx *c, uint32_t len) { (void)c; mesh_sys_respond(len); }

/* lockstep: the exchange page, only for the program with peer = "lockstep". */
static inline bool mesh_ctx_exchange(const mesh_ctx *c, mesh_exchange *out) {
    if (!c->lockstep) return false;
    mesh_view w = {MESH_WASM_W_EXCHANGE, 0, MESH_EX_BYTES};
    out->w = w;
    return true;
}

/* counters (manifest `counters`) */
static inline void mesh_ctx_count(const mesh_ctx *c, uint32_t i, uint32_t n) { (void)c; mesh_sys_count(i, n); }
static inline uint32_t mesh_ctx_counter(const mesh_ctx *c, uint32_t i) { (void)c; return mesh_sys_counter(i); }

/* Traps the instance (wasm `unreachable`): FAULT_TRAP, the slot FAILED. */
#define MESH_TRAP() __builtin_trap()

/* Declares the program: init(const mesh_ctx *) runs op 1 (OP_INIT); task(const mesh_ctx *, op, arg)
 * every other op, its result the task's. Exports mesh_init, mesh_task, mesh_rt_abi. */
#define MESH_PROGRAM(init_fn, task_fn)                                                                          \
    __attribute__((export_name("mesh_init"))) void mesh_init(void);                                             \
    void mesh_init(void) { init_fn(mesh_ctx_get()); }                                                           \
    __attribute__((export_name("mesh_task"))) uint32_t mesh_task(uint32_t op, uint32_t arg);                    \
    uint32_t mesh_task(uint32_t op, uint32_t arg) { return task_fn(mesh_ctx_get(), op, arg); }                  \
    __attribute__((export_name("mesh_rt_abi"))) uint32_t mesh_rt_abi(void);                                     \
    uint32_t mesh_rt_abi(void) { return MESH_RT_ABI; }

#endif
