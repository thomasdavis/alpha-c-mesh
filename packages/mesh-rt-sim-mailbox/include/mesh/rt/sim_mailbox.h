/* @ajax/mesh-rt-sim-mailbox: a mailbox in host memory, a program slot in it, and the host side
 * of the ticket ring, for running mesh programs on @ajax/mesh-rt-sim in tests.
 *
 * It does what meshd does for one slot, minimally and by the contract: the
 * header (magic, version, one system), the program table entry (written under
 * its PT_GEN seqlock), the extension (param, scratch), publication of tickets
 * (entry, then E_SEQ released, as meshd's publisher), completion by the marks,
 * the request window and the input queue, and a halt (P_SHUTDOWN, then the ring
 * rewound for a relaunch). Not a meshd: no policies, no blink, no snapshot. */
#ifndef MESH_RT_SIM_MAILBOX_H
#define MESH_RT_SIM_MAILBOX_H

#if !defined(__NVPTX__)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mesh/rt/sim.h"

typedef struct {
    const char *name;      /* [a-z0-9-]{1,15} */
    uint32_t view_off;     /* mailbox offset of the view; 0: VIEW_HEAP */
    uint32_t view_bytes;
    uint32_t nodes;        /* 0: 1 */
    uint32_t warps;        /* 0: 1 */
    uint32_t shared_bytes; /* the program's own, after RT_SHARED_BYTES */
    uint32_t reads_mask;
    uint32_t peer_mode;
    uint32_t input_off, input_len;
    uint32_t req_off, req_max;
    uint32_t resp_off, resp_max;
    uint32_t param;
    uint8_t *scratch; /* device memory of its own (host memory here) */
    uint32_t scratch_bytes;
} mesh_sim_prog;

/* A zeroed MAILBOX_BYTES mailbox (4 KiB aligned) with the header of a single
 * system: magic, version, H_PEER = PEER_NONE, H_SYSTEMS = 1, H_SMS = 1. NULL
 * when out of memory. */
uint8_t *mesh_sim_mailbox(void);
void mesh_sim_mailbox_free(uint8_t *mb);
/* Writes slot's table entry (state RUNNING) and extension; returns the view's address. */
uint8_t *mesh_sim_install(uint8_t *mb, uint32_t slot, const mesh_sim_prog *p);
/* Changes the slot's table state (e.g. EMPTY to unload it), under its seqlock. */
void mesh_sim_set_state(uint8_t *mb, uint32_t slot, uint32_t state);
/* Launches the slot's kernel with the program's nodes, warps and shared bytes. */
mesh_sim_launch *mesh_sim_launch_prog(uint8_t *mb, uint32_t slot, const mesh_sim_prog *p, mesh_sim_kernel kernel);
/* Publishes one ticket (op, arg) and returns it; it waits while 64 are in flight. */
uint32_t mesh_sim_publish(uint8_t *mb, uint32_t slot, uint32_t op, uint32_t arg);
/* Waits for ticket t's completion: true with its result, false after timeout_ms. */
bool mesh_sim_wait(uint8_t *mb, uint32_t slot, uint32_t t, uint32_t *result, uint32_t timeout_ms);
/* publish + wait (60 s); aborts the test on a timeout (a hung node). */
uint32_t mesh_sim_call(uint8_t *mb, uint32_t slot, uint32_t op, uint32_t arg);
/* A byte request (OP_REQ): the request into the window, P_REQ_LEN, P_REQ_SEQ + 1,
 * then the task; the response bytes (up to cap) into out. Returns the task's
 * result; *out_len the response length (P_RESP_LEN), or UINT32_MAX when the
 * program did not respond (P_RESP_SEQ != P_REQ_SEQ). */
uint32_t mesh_sim_request(uint8_t *mb, uint32_t slot, const void *req, uint32_t len, void *out, uint32_t cap, uint32_t *out_len);
/* Appends bytes to the slot's input queue (false when it lacks room). */
bool mesh_sim_input(uint8_t *mb, uint32_t slot, const void *bytes, uint32_t n);
/* Halts the launch at a task boundary (P_SHUTDOWN), joins it, and rewinds the
 * ring as meshd does (P_RING_HEAD = the tail, P_ONLINE = P_HALTED = 0,
 * P_SHUTDOWN = 0), so the slot can be launched again. */
void mesh_sim_halt(uint8_t *mb, uint32_t slot, mesh_sim_launch *l);
/* Gives slot an inbox (pipes, as the manifest's `inbox = "off:cap:rec"` and
 * `inbox_type`): cap records of rec bytes at view offset off. */
void mesh_sim_inbox(uint8_t *mb, uint32_t slot, uint32_t off, uint32_t cap, uint32_t rec, const char *type);
/* Connects producer -> consumer (the producer's `feeds`). */
void mesh_sim_feed(uint8_t *mb, uint32_t producer, uint32_t consumer);
/* The slot's control block (P_* fields) and view. */
uint8_t *mesh_sim_ctl(uint8_t *mb, uint32_t slot);
uint8_t *mesh_sim_view(uint8_t *mb, uint32_t slot);
/* Reads and writes of mailbox words (host side, little-endian, atomic where it matters). */
uint32_t mesh_sim_u32(const uint8_t *p);
uint64_t mesh_sim_u64(const uint8_t *p);
float mesh_sim_f32(const uint8_t *p);
void mesh_sim_set_u32(uint8_t *p, uint32_t v);
void mesh_sim_set_u64(uint8_t *p, uint64_t v);
void mesh_sim_set_f32(uint8_t *p, float v);

#endif /* !__NVPTX__ */
#endif
