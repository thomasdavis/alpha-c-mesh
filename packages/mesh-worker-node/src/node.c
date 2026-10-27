/* lib.rs's node_loop and run_nodes. */
#include "mesh/worker_node.h"

#include "mesh/abi/control_block.h"
#include "mesh/abi/node.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/trace.h"

#include <pthread.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

atomic_bool mesh_worker_stop_all;

#define HEARTBEAT_NS 2000000u

static uint32_t relaxed(mesh_mailbox mb, size_t off) {
    return atomic_load_explicit((_Atomic uint32_t *)(void *)(mb.base + off), memory_order_relaxed);
}

static void nap_ns(long ns) {
    struct timespec t = {0, ns};
    nanosleep(&t, NULL);
}

/* Idle polling: spin right after work, then sleep longer the longer it stays idle. */
static void idle(uint64_t since) {
    uint64_t t = mesh_worker_now_ns() - since;
    if (t < 50000u) sched_yield();
    else if (t < 10000000u) nap_ns(20000);
    else if (t < 1000000000u) nap_ns(200000);
    else nap_ns(1000000);
}

static uint32_t cpu_of_this_thread(uint32_t node) {
#if defined(__linux__)
    int c = sched_getcpu();
    if (c >= 0) return (uint32_t)c;
#endif
    return node;
}

void mesh_worker_node_loop(mesh_mailbox mb, uint32_t slot, uint32_t node, const mesh_worker_program *p, const atomic_bool *stop) {
    mesh_worker_ctx ctx;
    mesh_worker_ctx_new(mb, slot, node, &ctx);
    size_t ctl = mesh_prog_ctl(slot), rec = mesh_node_rec(slot, node);
    mesh_mailbox_set(mb, rec + MESH_N_SM, cpu_of_this_thread(node));
    mesh_mailbox_fetch_add(mb, ctl + MESH_P_ONLINE, 1);
    uint32_t beat = relaxed(mb, rec + MESH_N_HEARTBEAT);
    uint64_t beat_at = mesh_worker_now_ns(), idle_since = beat_at;
    for (;;) {
        uint32_t t;
        for (;;) {
            t = mesh_mailbox_get(mb, ctl + MESH_P_RING_HEAD);
            size_t e = ctl + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING);
            if (mesh_mailbox_get(mb, e + MESH_E_SEQ) == t + 1) {
                if (mesh_mailbox_cas(mb, ctl + MESH_P_RING_HEAD, t, t + 1) == t) break;
                continue;
            }
            /* Nothing published: a ticket published before the shutdown was claimed above first. */
            if (relaxed(mb, ctl + MESH_P_SHUTDOWN) != 0 || (stop && atomic_load_explicit(stop, memory_order_relaxed)) ||
                atomic_load_explicit(&mesh_worker_stop_all, memory_order_relaxed))
                goto halt;
            uint64_t now = mesh_worker_now_ns();
            if (now - beat_at >= HEARTBEAT_NS) {
                beat_at = now;
                mesh_mailbox_set(mb, rec + MESH_N_HEARTBEAT, ++beat);
            }
            idle(idle_since);
        }
        size_t e = ctl + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING);
        uint32_t op = relaxed(mb, e + MESH_E_OP), arg = relaxed(mb, e + MESH_E_ARG), cslot = relaxed(mb, e + MESH_E_SLOT) % MESH_PROG_RESULTS;
        ctx.ticket = t;
        uint64_t start = mesh_worker_now_ns();
        uint32_t result;
        if (op == MESH_OP_INIT) {
            if (p->init) p->init(&ctx, p->user);
            result = MESH_R_NONE;
        } else {
            result = p->task(&ctx, op, arg, p->user);
        }
        uint64_t end = mesh_worker_now_ns();
        mesh_mailbox_set(mb, ctl + MESH_P_RESULTS + 4 * cslot, result);
        mesh_mailbox_publish(mb, ctl + MESH_P_MARKS + 4 * cslot, t + 1);
        mesh_mailbox_fetch_add(mb, ctl + MESH_P_DONE, 1);
        mesh_mailbox_set(mb, rec + MESH_N_TASKS, relaxed(mb, rec + MESH_N_TASKS) + 1);
        mesh_mailbox_set(mb, rec + MESH_N_LAST_OP, op);
        mesh_mailbox_set64(mb, rec + MESH_N_BUSY_NS, mesh_mailbox_get64(mb, rec + MESH_N_BUSY_NS) + (end - start));
        mesh_mailbox_set(mb, rec + MESH_N_HEARTBEAT, ++beat);
        uint32_t n = mesh_mailbox_fetch_add(mb, ctl + MESH_P_TRACE_N, 1);
        size_t tr = mesh_trace_rec(slot, n);
        mesh_mailbox_set(mb, tr, slot | node << 8 | op << 16);
        mesh_mailbox_set(mb, tr + 4, t);
        mesh_mailbox_set(mb, tr + 8, (uint32_t)start);
        mesh_mailbox_set(mb, tr + 12, (uint32_t)end);
        idle_since = mesh_worker_now_ns();
    }
halt:
    mesh_mailbox_fetch_add(mb, ctl + MESH_P_HALTED, 1);
}

typedef struct {
    mesh_mailbox mb;
    uint32_t slot, node;
    const mesh_worker_program *p;
    const atomic_bool *stop;
} node_arg;

static void *node_main(void *a) {
    node_arg *n = a;
    mesh_worker_node_loop(n->mb, n->slot, n->node, n->p, n->stop);
    return NULL;
}

void mesh_worker_run_nodes(mesh_mailbox mb, uint32_t slot, const mesh_worker_program *p, const atomic_bool *stop) {
    uint32_t e[32];
    mesh_worker_entry(mb, slot, e);
    uint32_t nodes = e[MESH_PT_NODES / 4];
    if (nodes < 1) nodes = 1;
    if (nodes > MESH_PROG_MAX_NODES) nodes = MESH_PROG_MAX_NODES;
    pthread_t th[MESH_PROG_MAX_NODES];
    node_arg args[MESH_PROG_MAX_NODES];
    uint32_t started = 0;
    for (uint32_t n = 0; n < nodes; n++) {
        args[n] = (node_arg){mb, slot, n, p, stop};
        if (pthread_create(&th[n], NULL, node_main, &args[n]) != 0) {
            fputs("mesh-worker: a node thread could not start\n", stderr);
            abort();
        }
        started++;
    }
    for (uint32_t n = 0; n < started; n++) pthread_join(th[n], NULL);
}
