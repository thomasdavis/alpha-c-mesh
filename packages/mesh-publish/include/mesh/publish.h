/* @ajax/mesh-publish: the generic task publisher (publish.rs; contract v2).
 *
 * One ring per program slot, and the manifest's policy deciding what meshd
 * publishes on its own: continuous (a batch whenever the previous one
 * completed, or topped up to `fanout` in flight with in_flight = "free"),
 * periodic (`fanout` tasks at `hz`), on_demand (nothing; only task/req). The
 * arg is the index 0..fanout or the view's u32 at `arg_off` (+ index).
 * Nothing here knows any program: a new program needs no meshd change.
 *
 * Ticket rules: ticket t uses ring entry and completion slot t % 64; t may be
 * published only once no outstanding ticket is 64 or more behind. */
#ifndef MESH_PUBLISH_H
#define MESH_PUBLISH_H

#include "mesh/abi.h"
#include "mesh/mailbox.h"
#include "mesh/manifest.h"
#include "mesh/reply.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Who waits for a ticket (publish.rs Kind). */
typedef enum {
    MESH_TK_AUTO,      /* published by the policy */
    MESH_TK_INIT,      /* OP_INIT before anything else */
    MESH_TK_TASK,      /* `task <name> <op> <arg>`: reply gets the result */
    MESH_TK_REQ,       /* a byte request (OP_REQ) with its P_REQ_SEQ: reply gets the response */
    MESH_TK_FOLLOW,    /* placement: a part of a split frame (OP_FOLLOW) */
    MESH_TK_JOB,       /* a job's item: lease, job, item */
    MESH_TK_JOB_SETUP, /* a job's setup task: job, op, arg */
} mesh_tk;

typedef struct {
    mesh_tk tk;
    mesh_reply *reply; /* TASK, REQ: the waiter (owned) */
    uint64_t seq;      /* REQ */
    uint64_t lease, job, item; /* JOB (job also for JOB_SETUP) */
    uint32_t op, arg;          /* JOB_SETUP */
} mesh_kind;

typedef struct {
    uint32_t ticket;
    mesh_kind kind;
} mesh_out;

/* A completed ticket and its result. */
typedef struct {
    mesh_out out;
    uint32_t result;
} mesh_done;

/* Slot s's ring, host side; the driver thread is its only publisher. */
typedef struct {
    size_t ctl;
    uint32_t tail; /* the next ticket to publish */
    mesh_out *inflight; /* in publication order */
    size_t n, cap;
} mesh_pring;

static inline mesh_kind mesh_kind_of(mesh_tk tk) {
    mesh_kind k = {tk, NULL, 0, 0, 0, 0, 0, 0};
    return k;
}

/* A fresh ring (the control block was zeroed). */
void mesh_pring_init(mesh_pring *r, uint32_t slot);
void mesh_pring_free(mesh_pring *r); /* drops outstanding waiters unanswered */
/* Tickets publishable now (the oldest outstanding one bounds the window). */
uint32_t mesh_pring_room(const mesh_pring *r);
bool mesh_pring_can_publish(const mesh_pring *r);
/* Publishes; true with *ticket. False when full: the kind stays the caller's. */
bool mesh_pring_publish(mesh_pring *r, const mesh_mailbox *mb, uint32_t op, uint32_t arg, mesh_kind kind, uint32_t *ticket);
/* Completed tickets with their results, in publication order: *done (free it), *n. */
void mesh_pring_collect(mesh_pring *r, const mesh_mailbox *mb, mesh_done **done, size_t *n);
bool mesh_pring_idle(const mesh_pring *r);
uint32_t mesh_pring_auto_in_flight(const mesh_pring *r);
uint32_t mesh_pring_follow_in_flight(const mesh_pring *r);
size_t mesh_pring_in_flight(const mesh_pring *r);
bool mesh_pring_has_req(const mesh_pring *r);
bool mesh_pring_has_init(const mesh_pring *r);
/* Every node halted: the outstanding tickets will never run. They are
 * returned (*outs, free it), and P_RING_HEAD goes back to the tail. */
void mesh_pring_rewind(mesh_pring *r, const mesh_mailbox *mb, mesh_out **outs, size_t *n);

/* How many auto tasks the policy wants published now (of a batch of n);
 * advances *last (the periodic clock, ns) when it publishes. */
uint32_t mesh_due(const mesh_manifest *m, uint32_t n, const mesh_pring *r, int64_t *last, int64_t now);
/* Publishes what due asks for; returns the count. A batch goes out whole or
 * waits for ring room; in_flight = "free" publishes what fits. */
uint32_t mesh_publish_auto(const mesh_mailbox *mb, const mesh_manifest *m, uint32_t view_off, mesh_pring *r, int64_t *last, int64_t now);
/* publish_auto for the args in `parts` (bit b: arg b) of each batch only, ascending. */
uint32_t mesh_publish_parts(const mesh_mailbox *mb, const mesh_manifest *m, uint32_t view_off, mesh_pring *r, int64_t *last,
                            int64_t now, uint32_t parts);

#endif
