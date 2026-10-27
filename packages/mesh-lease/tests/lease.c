/* The Rust's lease tests (mesh-host lease.rs), one for one: lanes sharing a ring and halting, a
 * gated lane waiting for the blink, the bounded lockstep exchange. */
#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/header.h"
#include "mesh/abi/node.h"
#include "mesh/abi/ring.h"
#include "mesh/check.h"
#include "mesh/lease.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>

/* Squares the arg on node `first`; faults on op 9. */
static size_t sq_max(void *self) {
    (void)self;
    return 4;
}
static bool sq_run(void *self, mesh_lease *l, const mesh_taken *taken, size_t n) {
    (void)self;
    for (size_t i = 0; i < n; i++) {
        const mesh_taken *k = &taken[i];
        if (k->op == 9) {
            mesh_lease_fault(l, MESH_FAULT_TRAP | 7u << 8);
            return false;
        }
        uint64_t t0 = mesh_host_ns();
        mesh_lease_complete(l, l->first, k, k->arg * k->arg, t0, mesh_host_ns());
    }
    return true;
}
static const mesh_runner SQ = {sq_max, sq_run, NULL};

typedef struct {
    mesh_lease lease;
    pthread_t th;
    _Atomic bool done;
    mesh_lane_stats stats;
} lane;

static void *lane_main(void *p) {
    lane *x = p;
    mesh_lease_run(&x->lease, &SQ, NULL);
    return NULL;
}

static void publish(mesh_mailbox mb, uint32_t slot, uint32_t t, uint32_t op, uint32_t arg) {
    size_t e = mesh_prog_ctl(slot) + MESH_P_RING + MESH_ENTRY_BYTES * (size_t)(t % MESH_PROG_RING);
    mesh_mailbox_set(mb, e + MESH_E_OP, op);
    mesh_mailbox_set(mb, e + MESH_E_ARG, arg);
    mesh_mailbox_set(mb, e + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_mailbox_publish(mb, e + MESH_E_SEQ, t + 1);
}

static uint32_t ctl(mesh_mailbox mb, uint32_t slot, size_t off) { return mesh_mailbox_get(mb, mesh_prog_ctl(slot) + off); }

static void lanes_share_a_ring_and_halt(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    uint32_t slot = 3;
    lane lanes[3] = {0};
    for (uint32_t n = 0; n < 3; n++) {
        mesh_lease_init(&lanes[n].lease, mb, slot, n, 1, false, &lanes[n].done, &lanes[n].stats);
        pthread_create(&lanes[n].th, NULL, lane_main, &lanes[n]);
    }
    while (ctl(mb, slot, MESH_P_ONLINE) < 3) sched_yield();
    /* H_BLINK does not hold ungated lanes. */
    mesh_mailbox_set(mb, MESH_H_BLINK, 1);
    /* At most a ring of tickets in flight (as the publisher keeps it). */
    uint64_t t0 = mesh_host_ns();
    for (uint32_t t = 0; t < 200; t++) {
        size_t mark = MESH_P_MARKS + 4 * (size_t)(t % MESH_PROG_RESULTS);
        while (t >= MESH_PROG_RING && ctl(mb, slot, mark) != t - MESH_PROG_RING + 1) {
            CHECK(mesh_host_ns() - t0 < 5000000000ull);
            sched_yield();
        }
        publish(mb, slot, t, 0, t);
    }
    while (ctl(mb, slot, MESH_P_DONE) < 200) {
        if (!CHECK(mesh_host_ns() - t0 < 5000000000ull)) break;
        sched_yield();
    }
    for (uint32_t t = 0; t < 200; t++) {
        size_t c = t % MESH_PROG_RESULTS;
        if (t + MESH_PROG_RESULTS >= 200) {
            CHECK_U(ctl(mb, slot, MESH_P_MARKS + 4 * c), t + 1);
            CHECK_U(ctl(mb, slot, MESH_P_RESULTS + 4 * c), t * t);
        }
    }
    uint32_t tasks = 0;
    for (uint32_t n = 0; n < 3; n++) tasks += mesh_mailbox_get(mb, mesh_node_rec(slot, n) + MESH_N_TASKS);
    CHECK_U(tasks, 200);
    CHECK_U(ctl(mb, slot, MESH_P_RING_HEAD), 200);
    CHECK_U(ctl(mb, slot, MESH_P_TRACE_N), 200);
    /* A fault: recorded once, the ticket left incomplete; then shutdown halts every lane. */
    publish(mb, slot, 200, 9, 0);
    while (ctl(mb, slot, MESH_P_FAULT) == 0) sched_yield();
    CHECK_U(ctl(mb, slot, MESH_P_FAULT), MESH_FAULT_TRAP | 7u << 8);
    mesh_mailbox_publish(mb, mesh_prog_ctl(slot) + MESH_P_SHUTDOWN, 1);
    for (uint32_t n = 0; n < 3; n++) {
        pthread_join(lanes[n].th, NULL);
        CHECK(atomic_load(&lanes[n].done));
    }
    CHECK_U(ctl(mb, slot, MESH_P_HALTED), 3);
    CHECK_U(ctl(mb, slot, MESH_P_DONE), 200);
    free(mb.base);
}

static void gated_lane_waits_for_the_blink(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    mesh_mailbox_set(mb, MESH_H_BLINK, 1);
    lane x = {0};
    mesh_lease_init(&x.lease, mb, 0, 0, 2, true, &x.done, &x.stats);
    pthread_create(&x.th, NULL, lane_main, &x);
    publish(mb, 0, 0, 0, 5);
    struct timespec ms20 = {0, 20000000};
    nanosleep(&ms20, NULL);
    CHECK_U(ctl(mb, 0, MESH_P_ONLINE), 2);
    CHECK_U(ctl(mb, 0, MESH_P_DONE), 0);
    mesh_mailbox_set(mb, MESH_H_BLINK, 0);
    while (ctl(mb, 0, MESH_P_DONE) < 1) sched_yield();
    CHECK_U(ctl(mb, 0, MESH_P_RESULTS), 25);
    mesh_mailbox_publish(mb, mesh_prog_ctl(0) + MESH_P_SHUTDOWN, 1);
    pthread_join(x.th, NULL);
    CHECK_U(ctl(mb, 0, MESH_P_HALTED), 2);
    free(mb.base);
}

static void exchange_waits_bounded(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    lane x = {0};
    mesh_lease_init(&x.lease, mb, 0, 0, 1, false, &x.done, &x.stats);
    CHECK_U(mesh_lease_exchange(&x.lease, 1), MESH_ROUND_TIMEOUT); /* single: no round */
    mesh_mailbox_set(mb, MESH_H_GROUP_RANK, 1);
    mesh_mailbox_set(mb, MESH_H_GROUP_N, 2);
    mesh_mailbox_publish64(mb, mesh_ex_counter(0), 4);
    CHECK_U(mesh_lease_exchange(&x.lease, 3), MESH_ROUND_OK);
    CHECK_U(mesh_mailbox_get64(mb, mesh_ex_counter(1)), 3);
    uint64_t t0 = mesh_host_ns();
    CHECK_U(mesh_lease_exchange(&x.lease, 5), MESH_ROUND_TIMEOUT);
    CHECK(mesh_host_ns() - t0 >= (uint64_t)MESH_PEER_POLLS * MESH_PEER_POLL_NS);
    CHECK_U(atomic_load(&x.stats.rounds), 3);
    free(mb.base);
}

int main(void) {
    lanes_share_a_ring_and_halt();
    gated_lane_waits_for_the_blink();
    exchange_waits_bounded();
    return mesh_check_done();
}
