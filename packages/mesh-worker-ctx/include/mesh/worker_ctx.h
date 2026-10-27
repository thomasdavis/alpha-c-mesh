/* @ajax/mesh-worker-ctx: what a native worker program's init and task see
 * (mesh-worker lib.rs: Mb, View, Ctx): mesh-rt's Ctx API on a CPU thread.
 * A node is one thread: there are no lanes (lane() is 0, leader() true). */
#ifndef MESH_WORKER_CTX_H
#define MESH_WORKER_CTX_H

#include "mesh/mailbox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A process-local monotonic clock in ns (CLOCK_MONOTONIC; macOS: CLOCK_UPTIME_RAW). */
uint64_t mesh_worker_now_ns(void);

/* A byte window of the mailbox (a view or part of one).  Offsets are bytes
 * from its start; u32/f32 accessors need 4-aligned offsets, u64 8-aligned.
 * An access out of range (or misaligned) reads 0 / drops the write and
 * records P_FAULT (meshd fails the program, as on the GPU). */
typedef struct {
    uint8_t *base;
    uint32_t len;
    uint8_t *ctl; /* the caller's control block, where a fault is recorded */
} mesh_view;

uint32_t mesh_view_len(mesh_view v);
uint32_t mesh_view_u32(mesh_view v, uint32_t off);
float mesh_view_f32(mesh_view v, uint32_t off);
uint64_t mesh_view_u64(mesh_view v, uint32_t off);
uint8_t mesh_view_u8(mesh_view v, uint32_t off);
void mesh_view_set_u32(mesh_view v, uint32_t off, uint32_t x);
void mesh_view_set_f32(mesh_view v, uint32_t off, float x);
void mesh_view_set_u64(mesh_view v, uint32_t off, uint64_t x);
void mesh_view_set_u8(mesh_view v, uint32_t off, uint8_t x);
/* Atomic add on a u32 word (other nodes and processes may add too). */
uint32_t mesh_view_add_u32(mesh_view v, uint32_t off, uint32_t x);
/* Bulk copies (not atomic: publish them with a later set_u32). False (and a fault) when out of range. */
bool mesh_view_read(mesh_view v, uint32_t off, uint8_t *out, size_t n);
bool mesh_view_write(mesh_view v, uint32_t off, const uint8_t *b, size_t n);
bool mesh_view_write_f32s(mesh_view v, uint32_t off, const float *x, size_t n);
bool mesh_view_read_f32s(mesh_view v, uint32_t off, float *out, size_t n);
/* A sub-window [off, off + len); false (and a fault) when out of range. */
bool mesh_view_sub(mesh_view v, uint32_t off, uint32_t len, mesh_view *out);

/* Node `node` of slot `slot` (its entry read at creation, under the seqlock). */
typedef struct {
    mesh_mailbox mb;
    size_t ctl;
    mesh_view view;
    uint32_t slot, node, nodes, peer_mode;
    uint32_t input_off, input_len, req_off, req_max, resp_off, resp_max;
    uint32_t ticket; /* the running task's ring ticket; the node loop sets it */
} mesh_worker_ctx;

void mesh_worker_ctx_new(mesh_mailbox mb, uint32_t slot, uint32_t node, mesh_worker_ctx *out);
/* One stable copy of slot s's table entry (PT_GEN even and unchanged), PT_BYTES / 4 words. */
void mesh_worker_entry(mesh_mailbox mb, uint32_t s, uint32_t out[32]);

/* ids */
static inline uint32_t mesh_ctx_slot(const mesh_worker_ctx *c) { return c->slot; }
static inline uint32_t mesh_ctx_node(const mesh_worker_ctx *c) { return c->node; }
static inline uint32_t mesh_ctx_nodes(const mesh_worker_ctx *c) { return c->nodes; }
static inline uint32_t mesh_ctx_lane(const mesh_worker_ctx *c) { (void)c; return 0; }
static inline bool mesh_ctx_leader(const mesh_worker_ctx *c) { (void)c; return true; }
static inline uint32_t mesh_ctx_ticket(const mesh_worker_ctx *c) { return c->ticket; }
static inline uint32_t mesh_ctx_peer_mode(const mesh_worker_ctx *c) { return c->peer_mode; }
uint32_t mesh_ctx_system(const mesh_worker_ctx *c);  /* H_PEER */
uint32_t mesh_ctx_systems(const mesh_worker_ctx *c); /* H_SYSTEMS */
/* The lockstep group's (rank, size), or false (a native program is never lockstep). */
bool mesh_ctx_group(const mesh_worker_ctx *c, uint32_t *rank, uint32_t *size);
uint32_t mesh_ctx_param(const mesh_worker_ctx *c); /* PX_PARAM */
uint32_t mesh_ctx_sms(const mesh_worker_ctx *c);   /* H_SMS */

/* own view */
static inline mesh_view mesh_ctx_view(const mesh_worker_ctx *c) { return c->view; }
static inline uint32_t mesh_ctx_view_len(const mesh_worker_ctx *c) { return c->view.len; }
static inline uint32_t mesh_ctx_view_u32(const mesh_worker_ctx *c, uint32_t off) { return mesh_view_u32(c->view, off); }
static inline float mesh_ctx_view_f32(const mesh_worker_ctx *c, uint32_t off) { return mesh_view_f32(c->view, off); }
static inline uint64_t mesh_ctx_view_u64(const mesh_worker_ctx *c, uint32_t off) { return mesh_view_u64(c->view, off); }
static inline uint8_t mesh_ctx_view_u8(const mesh_worker_ctx *c, uint32_t off) { return mesh_view_u8(c->view, off); }
static inline void mesh_ctx_set_u32(const mesh_worker_ctx *c, uint32_t off, uint32_t v) { mesh_view_set_u32(c->view, off, v); }
static inline void mesh_ctx_set_f32(const mesh_worker_ctx *c, uint32_t off, float v) { mesh_view_set_f32(c->view, off, v); }
static inline void mesh_ctx_set_u64(const mesh_worker_ctx *c, uint32_t off, uint64_t v) { mesh_view_set_u64(c->view, off, v); }
static inline void mesh_ctx_set_u8(const mesh_worker_ctx *c, uint32_t off, uint8_t v) { mesh_view_set_u8(c->view, off, v); }

/* Other programs: slot s's view if `reads` lists it and the slot holds a
 * program (the mask re-read at each call); false otherwise (an undeclared
 * read also records FAULT_PEER_VIEW). */
bool mesh_ctx_peer_view(const mesh_worker_ctx *c, uint32_t s, mesh_view *out);
/* The slot holding program name: true and *slot, or false. */
bool mesh_ctx_find(const mesh_worker_ctx *c, const char *name, uint32_t *slot);
uint32_t mesh_ctx_prog_state(const mesh_worker_ctx *c, uint32_t s);
/* Slot s's name into out (at least 17 bytes). */
void mesh_ctx_prog_name(const mesh_worker_ctx *c, uint32_t s, char out[17]);
/* The whole mailbox, for a READS_ALL program; false otherwise. */
bool mesh_ctx_mailbox(const mesh_worker_ctx *c, mesh_mailbox *out);

/* input queue (manifest `input`) */
uint32_t mesh_ctx_input_pending(const mesh_worker_ctx *c);
/* The next input byte; false when empty.  Nodes may pop concurrently. */
bool mesh_ctx_input_pop(const mesh_worker_ctx *c, uint8_t *out);

/* pipes (manifest `inbox`, `feeds`) */
/* Pushes one record into slot s's inbox: PIPE_OK, PIPE_NONE (full: keep it,
 * try later), or a negative PIPE_* code.  A slot this program does not feed,
 * or a wrong size, records FAULT_PIPE. */
int32_t mesh_ctx_push_code(const mesh_worker_ctx *c, uint32_t s, const uint8_t *rec, size_t n);
bool mesh_ctx_push(const mesh_worker_ctx *c, uint32_t s, const uint8_t *rec, size_t n);
/* Pops the own inbox's next record into out (its record size): false when empty. */
bool mesh_ctx_pop(const mesh_worker_ctx *c, uint8_t *out, size_t n);
/* Records waiting in slot s's inbox: true and *len, or false. */
bool mesh_ctx_pipe_len(const mesh_worker_ctx *c, uint32_t s, uint32_t *len);
static inline uint64_t mesh_ctx_host_ns(const mesh_worker_ctx *c) { (void)c; return mesh_worker_now_ns(); }

/* request window (manifest `request`, op OP_REQ) */
mesh_view mesh_ctx_request(const mesh_worker_ctx *c);
mesh_view mesh_ctx_response(const mesh_worker_ctx *c);
/* Publishes len response bytes (P_RESP_LEN, then P_RESP_SEQ = P_REQ_SEQ, release). */
void mesh_ctx_respond(const mesh_worker_ctx *c, uint32_t len);

/* counters (manifest `counters`) */
void mesh_ctx_count(const mesh_worker_ctx *c, uint32_t i, uint32_t n);
uint32_t mesh_ctx_counter(const mesh_worker_ctx *c, uint32_t i);
/* Asks meshd to start/stop slot s (P_RUN_REQ): READS_ALL programs only. */
bool mesh_ctx_request_run(const mesh_worker_ctx *c, uint32_t s, bool on);

#endif
