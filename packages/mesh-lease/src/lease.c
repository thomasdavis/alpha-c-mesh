/* The node loop, completion, faults, the lockstep round. */
#include "mesh/lease.h"

#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/header.h"
#include "mesh/abi/node.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/trace.h"

#include <sched.h>
#include <stdlib.h>
#include <time.h>

uint64_t mesh_host_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void sleep_ns(long ns) {
    struct timespec ts = {0, ns};
    nanosleep(&ts, NULL);
}

void mesh_lease_init(mesh_lease *l, mesh_mailbox mb, uint32_t slot, uint32_t first, uint32_t count, bool blink_gate,
                     _Atomic bool *done, mesh_lane_stats *stats) {
    l->mb = mb;
    l->slot = slot;
    l->first = first;
    l->count = count;
    l->blink_gate = blink_gate;
    l->done = done;
    l->stats = stats;
    l->beat_at = mesh_host_ns();
}

/* Up to max published tickets in order, each claimed by a CAS on P_RING_HEAD. */
static size_t take(const mesh_lease *l, size_t max, mesh_taken *out) {
    mesh_mailbox mb = l->mb;
    size_t n = 0;
    if (l->blink_gate && mesh_mailbox_get(mb, MESH_H_BLINK) != 0) return 0;
    size_t head = mesh_lease_ctl(l, MESH_P_RING_HEAD);
    while (n < max) {
        uint32_t t = mesh_mailbox_get(mb, head);
        size_t e = mesh_lease_ctl(l, MESH_P_RING + MESH_ENTRY_BYTES * (size_t)(t % MESH_PROG_RING));
        if (mesh_mailbox_get(mb, e + MESH_E_SEQ) != t + 1u) break;
        /* Read the entry before the claim: once head moves past t the publisher may reuse it. */
        mesh_taken k = {t, mesh_mailbox_get(mb, e + MESH_E_OP), mesh_mailbox_get(mb, e + MESH_E_ARG),
                        mesh_mailbox_get(mb, e + MESH_E_SLOT) % MESH_PROG_RESULTS};
        if (mesh_mailbox_cas(mb, head, t, t + 1u) == t) out[n++] = k;
    }
    return n;
}

/* Nothing published: heartbeat like idle nodes, poll again soon (sooner right after work: the
 * publisher's next batch usually follows at once). True: this turn was a heartbeat. */
static bool idle(mesh_lease *l, uint64_t since) {
    uint64_t now = mesh_host_ns();
    bool beat = now - l->beat_at >= MESH_LEASE_HEARTBEAT_NS;
    if (beat) {
        l->beat_at = now;
        for (uint32_t n = l->first; n < l->first + l->count; n++) {
            size_t r = mesh_node_rec(l->slot, n) + MESH_N_HEARTBEAT;
            mesh_mailbox_set(l->mb, r, mesh_mailbox_get(l->mb, r) + 1u);
        }
    }
    uint64_t idle_ns = now - since;
    if (idle_ns < 200000) sched_yield();
    else if (idle_ns < 50000000) sleep_ns(30000);
    else sleep_ns(250000);
    return beat;
}

void mesh_lease_run(mesh_lease *l, const mesh_runner *runner, void *self) {
    mesh_mailbox mb = l->mb;
    for (uint32_t n = l->first; n < l->first + l->count; n++) mesh_mailbox_set(mb, mesh_node_rec(l->slot, n) + MESH_N_SM, n);
    atomic_thread_fence(memory_order_seq_cst);
    mesh_mailbox_fetch_add(mb, mesh_lease_ctl(l, MESH_P_ONLINE), l->count);
    size_t max = runner->max_take(self);
    if (max > MESH_PROG_RING) max = MESH_PROG_RING;
    if (max == 0) max = 1;
    mesh_taken taken[MESH_PROG_RING];
    mesh_words w = mesh_mailbox_words(mb);
    uint64_t idle_since = mesh_host_ns();
    for (;;) {
        bool shutdown = mesh_mailbox_get(mb, mesh_lease_ctl(l, MESH_P_SHUTDOWN)) != 0;
        size_t n = take(l, max, taken);
        if (n == 0) {
            /* A ticket published before the shutdown still runs (above); now none is. */
            if (shutdown) break;
            if (idle(l, idle_since) && runner->heartbeat) runner->heartbeat(self);
            continue;
        }
        uint32_t parity = 0;
        if (l->blink_gate) parity = mesh_pipe_flight_begin(&w, l->slot);
        /* false: the fault is recorded (P_FAULT); the driver fails the slot and shuts it down. */
        bool ok = runner->run(self, l, taken, n);
        if (l->blink_gate) mesh_pipe_flight_end(&w, l->slot, parity);
        if (ok) idle_since = mesh_host_ns();
    }
    atomic_thread_fence(memory_order_seq_cst);
    mesh_mailbox_fetch_add(mb, mesh_lease_ctl(l, MESH_P_HALTED), l->count);
    atomic_store(l->done, true);
}

void mesh_lease_complete(mesh_lease *l, uint32_t node, const mesh_taken *k, uint32_t result, uint64_t start, uint64_t end) {
    mesh_mailbox mb = l->mb;
    mesh_mailbox_set(mb, mesh_lease_ctl(l, MESH_P_RESULTS + 4 * (size_t)k->cslot), result);
    mesh_mailbox_publish(mb, mesh_lease_ctl(l, MESH_P_MARKS + 4 * (size_t)k->cslot), k->t + 1u);
    mesh_mailbox_fetch_add(mb, mesh_lease_ctl(l, MESH_P_DONE), 1);
    size_t r = mesh_node_rec(l->slot, node);
    mesh_mailbox_set(mb, r + MESH_N_TASKS, mesh_mailbox_get(mb, r + MESH_N_TASKS) + 1u);
    mesh_mailbox_set(mb, r + MESH_N_LAST_OP, k->op);
    mesh_mailbox_set64(mb, r + MESH_N_BUSY_NS, mesh_mailbox_get64(mb, r + MESH_N_BUSY_NS) + (end > start ? end - start : 0));
    mesh_mailbox_set(mb, r + MESH_N_HEARTBEAT, mesh_mailbox_get(mb, r + MESH_N_HEARTBEAT) + 1u);
    uint32_t tn = mesh_mailbox_fetch_add(mb, mesh_lease_ctl(l, MESH_P_TRACE_N), 1);
    size_t tr = mesh_trace_rec(l->slot, tn);
    mesh_mailbox_set(mb, tr, l->slot | node << 8 | k->op << 16);
    mesh_mailbox_set(mb, tr + 4, k->t);
    mesh_mailbox_set(mb, tr + 8, (uint32_t)start);
    mesh_mailbox_set(mb, tr + 12, (uint32_t)end);
    if (l->stats) atomic_fetch_add(&l->stats->tasks, 1);
}

void mesh_lease_fault(const mesh_lease *l, uint32_t code) {
    size_t f = mesh_lease_ctl(l, MESH_P_FAULT);
    if (mesh_mailbox_get(l->mb, f) == 0) mesh_mailbox_cas(l->mb, f, 0, code);
}

uint32_t mesh_lease_exchange(mesh_lease *l, uint64_t n) {
    uint64_t waited = 0;
    uint32_t st = mesh_round(l->mb, n, &waited);
    if (l->stats) {
        atomic_fetch_add(&l->stats->rounds, 1);
        atomic_fetch_add(&l->stats->round_wait_ns, waited);
    }
    return st;
}

uint32_t mesh_round(mesh_mailbox mb, uint64_t n, uint64_t *waited_ns) {
    uint32_t me = mesh_mailbox_get(mb, MESH_H_GROUP_RANK), sys = mesh_mailbox_get(mb, MESH_H_GROUP_N);
    if (waited_ns) *waited_ns = 0;
    if (!(sys >= 2 && sys <= MESH_MAX_SYSTEMS && me < sys)) return MESH_ROUND_TIMEOUT;
    atomic_thread_fence(memory_order_seq_cst);
    if (mesh_mailbox_get64(mb, mesh_ex_counter(me)) < n) mesh_mailbox_publish64(mb, mesh_ex_counter(me), n);
    uint64_t t0 = mesh_host_ns();
    uint64_t budget = (uint64_t)MESH_PEER_POLLS * MESH_PEER_POLL_NS;
    bool ok;
    for (;;) {
        ok = true;
        for (uint32_t j = 0; j < sys && ok; j++)
            if (j != me && mesh_mailbox_get64(mb, mesh_ex_counter(j)) < n) ok = false;
        if (ok) break;
        if (mesh_host_ns() - t0 >= budget) break;
    }
    atomic_thread_fence(memory_order_seq_cst);
    if (waited_ns) *waited_ns = mesh_host_ns() - t0;
    return ok ? MESH_ROUND_OK : MESH_ROUND_TIMEOUT;
}
