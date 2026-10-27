/* A slot of a test mailbox, its ticket ring from the host side, and its nodes
 * on a thread (exec's mesh_worker_run_nodes), for native programs' tests. */
#ifndef NATIVE_TEST_HARNESS_H
#define NATIVE_TEST_HARNESS_H
#include "mesh/abi/control_block.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/pipe_table.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/pipe_hash.h"
#include "mesh/check.h"
#include "mesh/mailbox.h"
#include "mesh/worker_node.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static inline void h_install(mesh_mailbox mb, uint32_t s, const char *name, uint32_t nodes, uint32_t view_off, uint32_t view_bytes) {
    size_t e = mesh_prog_entry(s);
    const uint32_t kv[][2] = {{MESH_PT_VIEW_OFF, view_off}, {MESH_PT_VIEW_BYTES, view_bytes}, {MESH_PT_NODES, nodes}, {MESH_PT_STATE, MESH_ST_RUNNING}};
    for (size_t i = 0; i < 4; i++) mesh_mailbox_set(mb, e + kv[i][0], kv[i][1]);
    uint8_t n[16] = {0};
    memcpy(n, name, strlen(name));
    mesh_mailbox_write(mb, e + MESH_PT_NAME, n, 16, 16);
}
static inline void h_inbox(mesh_mailbox mb, uint32_t s, uint32_t off, uint32_t cap, uint32_t rec, const char *type) {
    size_t p = mesh_pipe_entry(s);
    mesh_mailbox_set(mb, p + MESH_PP_IN_OFF, off);
    mesh_mailbox_set(mb, p + MESH_PP_IN_CAP, cap);
    mesh_mailbox_set(mb, p + MESH_PP_IN_REC, rec);
    mesh_mailbox_set(mb, p + MESH_PP_IN_TYPE, mesh_pipe_type_hash((const uint8_t *)type, strlen(type)));
}
static inline void h_feed(mesh_mailbox mb, uint32_t from, uint32_t to) {
    size_t p = mesh_pipe_entry(from);
    mesh_mailbox_set(mb, p + MESH_PP_FEEDS_MASK, mesh_mailbox_get(mb, p + MESH_PP_FEEDS_MASK) | 1u << to);
}

typedef struct {
    mesh_mailbox mb;
    uint32_t slot, next;
    mesh_worker_program prog;
    atomic_bool stop;
    pthread_t th;
} h_slot;

static inline void *h_runner(void *a) {
    h_slot *h = a;
    mesh_worker_run_nodes(h->mb, h->slot, &h->prog, &h->stop);
    return NULL;
}
static inline void h_start(h_slot *h) {
    atomic_store(&h->stop, false);
    pthread_create(&h->th, NULL, h_runner, h);
}
static inline void h_stop(h_slot *h) {
    mesh_mailbox_publish(h->mb, mesh_prog_ctl(h->slot) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT);
    pthread_join(h->th, NULL);
}
static inline uint32_t h_call(h_slot *h, uint32_t op, uint32_t arg) {
    uint32_t t = h->next++;
    size_t c = mesh_prog_ctl(h->slot), e = c + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING), k = t % MESH_PROG_RESULTS;
    mesh_mailbox_set(h->mb, e + MESH_E_OP, op);
    mesh_mailbox_set(h->mb, e + MESH_E_ARG, arg);
    mesh_mailbox_set(h->mb, e + MESH_E_SLOT, (uint32_t)k);
    mesh_mailbox_publish(h->mb, e + MESH_E_SEQ, t + 1);
    struct timespec t0, now;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (mesh_mailbox_get(h->mb, c + MESH_P_MARKS + 4 * k) != t + 1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - t0.tv_sec > 60) { fprintf(stderr, "ticket %u never completed\n", t); abort(); }
        sched_yield();
    }
    return mesh_mailbox_get(h->mb, c + MESH_P_RESULTS + 4 * k);
}
#endif
