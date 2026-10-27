/* @ajax/mesh-worker-node: mesh-rt's node loop on a CPU thread (mesh-worker
 * lib.rs node_loop, run_nodes).  One thread per node claims the next
 * published ticket (E_SEQ, acquire; then a CAS on P_RING_HEAD, the lease
 * lanes' protocol: several claimers share a ring and none holds an
 * unpublished ticket, so a halt leaves the head at the tail), runs the op and
 * completes it: P_RESULTS, P_MARKS (release), P_DONE, the node record, a
 * trace record.  It halts at P_SHUTDOWN (or a stop flag). */
#ifndef MESH_WORKER_NODE_H
#define MESH_WORKER_NODE_H

#include "mesh/worker_ctx.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* OP_INIT runs init (result R_NONE); every other op runs task. */
typedef void (*mesh_worker_init_fn)(const mesh_worker_ctx *ctx, void *user);
typedef uint32_t (*mesh_worker_task_fn)(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user);

typedef struct {
    mesh_worker_init_fn init;
    mesh_worker_task_fn task;
    void *user;
} mesh_worker_program;

/* Set when meshd is gone (EOF on stdin) or said `exit`: every node halts. */
extern atomic_bool mesh_worker_stop_all;

/* One node until P_SHUTDOWN (or *stop, or mesh_worker_stop_all). */
void mesh_worker_node_loop(mesh_mailbox mb, uint32_t slot, uint32_t node, const mesh_worker_program *p, const atomic_bool *stop);
/* Slot's PT_NODES nodes, one thread each, until they all halted. */
void mesh_worker_run_nodes(mesh_mailbox mb, uint32_t slot, const mesh_worker_program *p, const atomic_bool *stop);

#endif
