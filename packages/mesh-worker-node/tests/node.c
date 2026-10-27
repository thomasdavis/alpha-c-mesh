/* mesh-worker lib.rs's tests, one for one: the node loop runs a ring like
 * mesh-rt; faults, requests and peer views; the input queue pops once each. */
#include "mesh/check.h"
#include "mesh/worker_node.h"

#include "mesh/abi/control_block.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/input_queue.h"
#include "mesh/abi/node.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/ring.h"
#include "mesh/abi/trace.h"

#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The table entry meshd would write for a slot (view, nodes, request window). */
static void install(mesh_mailbox mb, uint32_t s, uint32_t nodes, uint32_t view_off, uint32_t view_bytes) {
    size_t e = mesh_prog_entry(s);
    const uint32_t kv[][2] = {{MESH_PT_VIEW_OFF, view_off}, {MESH_PT_VIEW_BYTES, view_bytes}, {MESH_PT_NODES, nodes}, {MESH_PT_STATE, MESH_ST_RUNNING},
                              {MESH_PT_REQ_OFF, 0x100}, {MESH_PT_REQ_MAX, 0x40}, {MESH_PT_RESP_OFF, 0x200}, {MESH_PT_RESP_MAX, 0x40}};
    for (size_t i = 0; i < 8; i++) mesh_mailbox_set(mb, e + kv[i][0], kv[i][1]);
    mesh_mailbox_set(mb, e + MESH_PT_NAME, 't' | 's' << 8 | 't' << 16);
}

static void publish(mesh_mailbox mb, uint32_t s, uint32_t t, uint32_t op, uint32_t arg) {
    size_t e = mesh_prog_ctl(s) + MESH_P_RING + MESH_ENTRY_BYTES * (t % MESH_PROG_RING);
    mesh_mailbox_set(mb, e + MESH_E_OP, op);
    mesh_mailbox_set(mb, e + MESH_E_ARG, arg);
    mesh_mailbox_set(mb, e + MESH_E_SLOT, t % MESH_PROG_RESULTS);
    mesh_mailbox_publish(mb, e + MESH_E_SEQ, t + 1);
}

static bool done(mesh_mailbox mb, uint32_t s, uint32_t t, uint32_t *result) {
    size_t c = mesh_prog_ctl(s), k = t % MESH_PROG_RESULTS;
    if (mesh_mailbox_get(mb, c + MESH_P_MARKS + 4 * k) != t + 1) return false;
    if (result) *result = mesh_mailbox_get(mb, c + MESH_P_RESULTS + 4 * k);
    return true;
}

static void init(const mesh_worker_ctx *c, void *u) {
    (void)u;
    mesh_ctx_set_u32(c, 0, 0x4f4c4548);
}
static uint32_t task(const mesh_worker_ctx *c, uint32_t op, uint32_t arg, void *u) {
    (void)u;
    if (op != MESH_OP_TASK) return MESH_R_BAD_OP;
    mesh_ctx_count(c, 0, 1);
    mesh_view_add_u32(mesh_ctx_view(c), 8, 1);
    return arg * 3 + 7;
}

typedef struct {
    mesh_mailbox mb;
    atomic_bool stop;
} run_arg;

static const mesh_worker_program prog = {init, task, NULL};
static void *runner(void *a) {
    run_arg *r = a;
    mesh_worker_run_nodes(r->mb, 3, &prog, &r->stop);
    return NULL;
}

static double secs(const struct timespec *t0) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)(t.tv_sec - t0->tv_sec) + (double)(t.tv_nsec - t0->tv_nsec) / 1e9;
}

static void nodes_run_the_ring_like_mesh_rt(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    install(mb, 3, 4, 0x100000, 0x1000);
    run_arg r = {mb, false};
    pthread_t th;
    pthread_create(&th, NULL, runner, &r);
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (mesh_mailbox_get(mb, mesh_prog_ctl(3) + MESH_P_ONLINE) < 4) {
        if (!CHECK(secs(&t0) < 5)) break;
        sched_yield();
    }
    publish(mb, 3, 0, MESH_OP_INIT, 0);
    for (uint32_t t = 1; t < 200; t++) {
        /* The publisher never runs PROG_RING ahead of the oldest outstanding ticket. */
        while (t >= MESH_PROG_RING && !done(mb, 3, t - MESH_PROG_RING, NULL)) sched_yield();
        publish(mb, 3, t, t == 150 ? 9 : MESH_OP_TASK, t);
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (mesh_mailbox_get(mb, mesh_prog_ctl(3) + MESH_P_DONE) < 200) {
        if (!CHECK(secs(&t0) < 5)) break;
        sched_yield();
    }
    mesh_mailbox_publish(mb, mesh_prog_ctl(3) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT);
    pthread_join(th, NULL);
    size_t c = mesh_prog_ctl(3);
    CHECK_U(mesh_mailbox_get(mb, c + MESH_P_HALTED), 4);
    CHECK_U(mesh_mailbox_get(mb, c + MESH_P_DONE), 200);
    CHECK_U(mesh_mailbox_get(mb, c + MESH_P_COUNTERS), 198);
    uint32_t res;
    CHECK(done(mb, 3, 199, &res) && res == 199 * 3 + 7);
    CHECK(done(mb, 3, 150, &res) && res == MESH_R_BAD_OP);
    CHECK_U(mesh_mailbox_get(mb, 0x100000), 0x4f4c4548);
    CHECK_U(mesh_mailbox_get(mb, 0x100008), 198);
    uint32_t tasks = 0;
    for (uint32_t n = 0; n < 4; n++) tasks += mesh_mailbox_get(mb, mesh_node_rec(3, n) + MESH_N_TASKS);
    CHECK_U(tasks, 200);
    CHECK_U(mesh_mailbox_get(mb, c + MESH_P_TRACE_N), 200);
    CHECK_U(mesh_mailbox_get(mb, c + MESH_P_FAULT), 0);
    /* The trace record of ticket 199: slot | node << 8 | op << 16, the ticket, start <= end. */
    size_t tr = 0;
    for (uint32_t n = 0; n < 200; n++)
        if (mesh_mailbox_get(mb, mesh_trace_rec(3, n) + 4) == 199) tr = mesh_trace_rec(3, n);
    CHECK(tr != 0);
    CHECK_U(mesh_mailbox_get(mb, tr) & 0xff, 3);
    CHECK(mesh_mailbox_get(mb, tr + 12) - mesh_mailbox_get(mb, tr + 8) < 1u << 31);
    free(mb.base);
}

static void faults_requests_and_peers(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    install(mb, 1, 1, 0x100000, 0x400);
    install(mb, 2, 1, 0x101000, 0x40);
    mesh_mailbox_set(mb, 0x101000, 77);
    mesh_worker_ctx c;
    mesh_worker_ctx_new(mb, 1, 0, &c);
    /* Bounds: a read past the view is 0 and records the first fault. */
    CHECK_U(mesh_ctx_view_u32(&c, 0x400), 0);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(1) + MESH_P_FAULT), MESH_FAULT_VIEW_BOUNDS | 0x400 << 8);
    mesh_ctx_set_u32(&c, 0x3fd, 1);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(1) + MESH_P_FAULT), MESH_FAULT_VIEW_BOUNDS | 0x400 << 8);
    mesh_mailbox_set(mb, mesh_prog_ctl(1) + MESH_P_FAULT, 0);
    /* Peer views need the reads mask (re-read per call). */
    mesh_view pv;
    CHECK(!mesh_ctx_peer_view(&c, 2, &pv));
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(1) + MESH_P_FAULT) & 0xff, MESH_FAULT_PEER_VIEW);
    mesh_mailbox_set(mb, mesh_prog_entry(1) + MESH_PT_READS_MASK, 1 << 2);
    CHECK(mesh_ctx_peer_view(&c, 2, &pv) && mesh_view_u32(pv, 0) == 77);
    uint32_t s = 99;
    CHECK(mesh_ctx_find(&c, "tst", &s) && s == 1);
    char name[17];
    mesh_ctx_prog_name(&c, 2, name);
    CHECK_S(name, "tst");
    /* The request window: request bytes, the response and its sequence. */
    mesh_mailbox_set(mb, mesh_prog_ctl(1) + MESH_P_REQ_LEN, 5);
    mesh_view_write(mesh_ctx_view(&c), 0x100, (const uint8_t *)"hello", 5);
    mesh_mailbox_publish64(mb, mesh_prog_ctl(1) + MESH_P_REQ_SEQ, 9);
    uint8_t b[5];
    CHECK(mesh_view_read(mesh_ctx_request(&c), 0, b, 5) && memcmp(b, "hello", 5) == 0);
    mesh_view_write(mesh_ctx_response(&c), 0, (const uint8_t *)"olleh", 5);
    mesh_ctx_respond(&c, 5);
    CHECK_U(mesh_mailbox_get64(mb, mesh_prog_ctl(1) + MESH_P_RESP_SEQ), 9);
    CHECK_U(mesh_mailbox_get(mb, mesh_prog_ctl(1) + MESH_P_RESP_LEN), 5);
    /* Bulk f32s and their bounds. */
    float f2[2] = {1.5f, -2.0f};
    CHECK(mesh_view_write_f32s(mesh_ctx_view(&c), 0x20, f2, 2));
    CHECK(mesh_ctx_view_f32(&c, 0x20) == 1.5f && mesh_ctx_view_f32(&c, 0x24) == -2.0f);
    CHECK(!mesh_view_write_f32s(mesh_ctx_view(&c), 0x3fc, f2, 2));
    free(mb.base);
}

typedef struct {
    mesh_mailbox mb;
    uint32_t node;
    uint8_t got[16];
    size_t n;
} popper;

static void *pop_all(void *a) {
    popper *p = a;
    mesh_worker_ctx c;
    mesh_worker_ctx_new(p->mb, 0, p->node, &c);
    uint8_t b;
    while (mesh_ctx_input_pop(&c, &b)) p->got[p->n++] = b;
    return NULL;
}

static void input_queue_pops_once_each(void) {
    mesh_mailbox mb = mesh_mailbox_test();
    install(mb, 0, 2, 0x100000, 0x1000);
    uint32_t off = 0x800, len = 64;
    mesh_mailbox_set(mb, mesh_prog_entry(0) + MESH_PT_INPUT_OFF, off);
    mesh_mailbox_set(mb, mesh_prog_entry(0) + MESH_PT_INPUT_LEN, len);
    size_t q = 0x100000 + off;
    const char *bytes = "abcdefgh";
    for (size_t i = 0; i < 8; i++) {
        size_t at = q + MESH_IQ_DATA + i;
        mesh_mailbox_set(mb, at & ~(size_t)3, mesh_mailbox_get(mb, at & ~(size_t)3) | (uint32_t)(uint8_t)bytes[i] << (8 * (at & 3)));
    }
    mesh_mailbox_publish(mb, q + MESH_IQ_HEAD, 8);
    mesh_worker_ctx c;
    mesh_worker_ctx_new(mb, 0, 0, &c);
    CHECK_U(mesh_ctx_input_pending(&c), 8);
    popper x = {mb, 0, {0}, 0}, y = {mb, 1, {0}, 0};
    pthread_t tx, ty;
    pthread_create(&tx, NULL, pop_all, &x);
    pthread_create(&ty, NULL, pop_all, &y);
    pthread_join(tx, NULL);
    pthread_join(ty, NULL);
    CHECK_U(x.n + y.n, 8);
    int seen[256] = {0};
    for (size_t i = 0; i < x.n; i++) seen[x.got[i]]++;
    for (size_t i = 0; i < y.n; i++) seen[y.got[i]]++;
    for (size_t i = 0; i < 8; i++) CHECK_I(seen[(uint8_t)bytes[i]], 1);
    uint8_t b;
    CHECK(!mesh_ctx_input_pop(&c, &b));
    free(mb.base);
}

int main(void) {
    nodes_run_the_ring_like_mesh_rt();
    faults_requests_and_peers();
    input_queue_pops_once_each();
    return mesh_check_done();
}
