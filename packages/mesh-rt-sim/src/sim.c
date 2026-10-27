/* The CPU implementation of mesh-rt's device primitives, and the launcher. */
#if !defined(__NVPTX__)

#include "mesh/rt/sim.h"
#include "mesh/abi/base.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    pthread_barrier_t bar;
    uint32_t xch[32];
} sim_warp;

typedef struct {
    uint32_t ctaid;
    uint32_t ntid;
    uint32_t smem_bytes;
    uint8_t *smem;
    pthread_barrier_t bar;
    sim_warp *warps;
} sim_node;

typedef struct {
    sim_node *node;
    uint32_t tid;
    mesh_sim_kernel kernel;
    uint8_t *mb;
    uint32_t slot;
    pthread_t thread;
    bool started;
} sim_thread;

struct mesh_sim_launch {
    uint32_t nodes;
    uint32_t threads; /* per node */
    sim_node *node;
    sim_thread *thr;
};

static _Thread_local sim_thread *self;

static sim_thread *me(void) {
    if (!self) {
        fprintf(stderr, "mesh-rt-sim: a device primitive was called outside a simulated thread\n");
        abort();
    }
    return self;
}

static _Atomic uint32_t *a32(const uint8_t *p) { return (_Atomic uint32_t *)(void *)(uintptr_t)p; }
static _Atomic uint64_t *a64(const uint8_t *p) { return (_Atomic uint64_t *)(void *)(uintptr_t)p; }

uint32_t mesh_rt_smid(void) { return me()->node->ctaid; }
uint32_t mesh_rt_tid(void) { return me()->tid; }
uint32_t mesh_rt_lane(void) { return me()->tid & 31u; }
uint32_t mesh_rt_dyn_smem(void) { return me()->node->smem_bytes; }
uint32_t mesh_rt_ntid(void) { return me()->node->ntid; }
uint32_t mesh_rt_ctaid(void) { return me()->node->ctaid; }

uint64_t mesh_rt_gtimer(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

uint32_t mesh_rt_ld_acquire(const uint8_t *p) { return atomic_load_explicit(a32(p), memory_order_acquire); }
uint64_t mesh_rt_ld_acquire64(const uint8_t *p) { return atomic_load_explicit(a64(p), memory_order_acquire); }
uint32_t mesh_rt_ld_relaxed(const uint8_t *p) { return atomic_load_explicit(a32(p), memory_order_relaxed); }
uint64_t mesh_rt_ld_relaxed64(const uint8_t *p) { return atomic_load_explicit(a64(p), memory_order_relaxed); }
void mesh_rt_st_release(uint8_t *p, uint32_t v) { atomic_store_explicit(a32(p), v, memory_order_release); }
void mesh_rt_st_release64(uint8_t *p, uint64_t v) { atomic_store_explicit(a64(p), v, memory_order_release); }
void mesh_rt_st_relaxed(uint8_t *p, uint32_t v) { atomic_store_explicit(a32(p), v, memory_order_relaxed); }
uint32_t mesh_rt_atom_add(uint8_t *p, uint32_t v) { return atomic_fetch_add_explicit(a32(p), v, memory_order_relaxed); }
uint32_t mesh_rt_atom_or(uint8_t *p, uint32_t v) { return atomic_fetch_or_explicit(a32(p), v, memory_order_relaxed); }
uint32_t mesh_rt_atom_and(uint8_t *p, uint32_t v) { return atomic_fetch_and_explicit(a32(p), v, memory_order_relaxed); }
uint32_t mesh_rt_atom_cas(uint8_t *p, uint32_t cmp, uint32_t desired) {
    atomic_compare_exchange_strong_explicit(a32(p), &cmp, desired, memory_order_relaxed, memory_order_relaxed);
    return cmp;
}
void mesh_rt_red_add(uint8_t *p, uint32_t v) { atomic_fetch_add_explicit(a32(p), v, memory_order_relaxed); }
void mesh_rt_fence_sc(void) { atomic_thread_fence(memory_order_seq_cst); }
void mesh_rt_fence_acq_rel(void) { atomic_thread_fence(memory_order_acq_rel); }

void mesh_rt_nanosleep(uint32_t ns) {
    struct timespec ts = {0, (long)ns};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static void wait_bar(pthread_barrier_t *b) {
    int r = pthread_barrier_wait(b);
    if (r != 0 && r != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr, "mesh-rt-sim: pthread_barrier_wait: %s\n", strerror(r));
        abort();
    }
}

static sim_warp *my_warp(void) {
    sim_thread *t = me();
    return &t->node->warps[t->tid >> 5];
}

void mesh_rt_warp_sync(void) { wait_bar(&my_warp()->bar); }
void mesh_rt_bar_sync(void) {
    atomic_thread_fence(memory_order_seq_cst);
    wait_bar(&me()->node->bar);
    atomic_thread_fence(memory_order_seq_cst);
}
uint8_t *mesh_rt_smem_base(void) { return me()->node->smem; }

/* Every lane deposits v; after the barrier each reads what it wants; the second
 * barrier keeps the next exchange from overwriting a value not yet read. */
static uint32_t exchange(uint32_t v, uint32_t (*pick)(const uint32_t *xch, uint32_t lane, uint32_t arg), uint32_t arg) {
    sim_warp *w = my_warp();
    uint32_t lane = me()->tid & 31u;
    w->xch[lane] = v;
    wait_bar(&w->bar);
    uint32_t r = pick(w->xch, lane, arg);
    wait_bar(&w->bar);
    return r;
}
static uint32_t pick_idx(const uint32_t *x, uint32_t lane, uint32_t src) { (void)lane; return x[src & 31u]; }
static uint32_t pick_bfly(const uint32_t *x, uint32_t lane, uint32_t m) { return x[(lane ^ m) & 31u]; }
static uint32_t pick_up(const uint32_t *x, uint32_t lane, uint32_t d) { return lane >= d ? x[lane - d] : x[lane]; }
static uint32_t pick_ballot(const uint32_t *x, uint32_t lane, uint32_t unused) {
    (void)lane, (void)unused;
    uint32_t r = 0;
    for (uint32_t i = 0; i < 32; i++)
        if (x[i]) r |= 1u << i;
    return r;
}

uint32_t mesh_rt_shfl_idx(uint32_t v, uint32_t src) { return exchange(v, pick_idx, src); }
uint32_t mesh_rt_shfl_bfly(uint32_t v, uint32_t m) { return exchange(v, pick_bfly, m); }
uint32_t mesh_rt_shfl_up(uint32_t v, uint32_t d) { return exchange(v, pick_up, d); }
uint32_t mesh_rt_ballot(bool p) { return exchange(p ? 1u : 0u, pick_ballot, 0); }

_Noreturn void mesh_rt_trap(void) {
    sim_thread *t = self;
    fprintf(stderr, "mesh-rt-sim: trap (node %u, thread %u)\n", t ? t->node->ctaid : 0u, t ? t->tid : 0u);
    abort();
}

/* f32 subnormal inputs and results flush to sign-preserving zero, as the
 * cluster float mode's .ftz instructions do (README "Cluster float mode"). */
static void ftz_on(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t csr;
    __asm__ __volatile__("stmxcsr %0" : "=m"(csr));
    csr |= 0x8040u; /* FTZ (bit 15) | DAZ (bit 6) */
    __asm__ __volatile__("ldmxcsr %0" ::"m"(csr));
#elif defined(__aarch64__)
    uint64_t fpcr;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (uint64_t)1 << 24; /* FZ */
    __asm__ __volatile__("msr fpcr, %0" ::"r"(fpcr));
#endif
}

static void *run(void *arg) {
    sim_thread *t = arg;
    self = t;
    ftz_on();
    t->kernel(t->mb, t->slot);
    self = NULL;
    return NULL;
}

static void free_launch(mesh_sim_launch *l) {
    for (uint32_t n = 0; n < l->nodes; n++) {
        sim_node *nd = &l->node[n];
        if (nd->warps) {
            for (uint32_t w = 0; w < l->threads / 32; w++) pthread_barrier_destroy(&nd->warps[w].bar);
            free(nd->warps);
        }
        pthread_barrier_destroy(&nd->bar);
        free(nd->smem);
    }
    free(l->node);
    free(l->thr);
    free(l);
}

mesh_sim_launch *mesh_sim_start(mesh_sim_kernel kernel, uint8_t *mb, uint32_t slot, uint32_t nodes, uint32_t warps,
                                uint32_t shared_bytes) {
    if (nodes == 0 || warps == 0 || warps > MESH_MAX_WARPS) return NULL;
    mesh_sim_launch *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->nodes = nodes;
    l->threads = 32 * warps;
    l->node = calloc(nodes, sizeof *l->node);
    l->thr = calloc((size_t)nodes * l->threads, sizeof *l->thr);
    if (!l->node || !l->thr) { free(l->node); free(l->thr); free(l); return NULL; }
    for (uint32_t n = 0; n < nodes; n++) {
        sim_node *nd = &l->node[n];
        nd->ctaid = n;
        nd->ntid = l->threads;
        nd->smem_bytes = MESH_RT_SHARED_BYTES + shared_bytes;
        nd->smem = aligned_alloc(16, ((size_t)nd->smem_bytes + 15) & ~(size_t)15);
        nd->warps = calloc(warps, sizeof *nd->warps);
        if (!nd->smem || !nd->warps) { fprintf(stderr, "mesh-rt-sim: out of memory\n"); abort(); }
        memset(nd->smem, 0, nd->smem_bytes);
        pthread_barrier_init(&nd->bar, NULL, l->threads);
        for (uint32_t w = 0; w < warps; w++) pthread_barrier_init(&nd->warps[w].bar, NULL, 32);
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, (size_t)1 << 20);
    for (uint32_t n = 0; n < nodes; n++) {
        for (uint32_t i = 0; i < l->threads; i++) {
            sim_thread *t = &l->thr[(size_t)n * l->threads + i];
            *t = (sim_thread){.node = &l->node[n], .tid = i, .kernel = kernel, .mb = mb, .slot = slot};
            int r = pthread_create(&t->thread, &attr, run, t);
            if (r != 0) {
                /* Threads already started would wait at a barrier forever. */
                fprintf(stderr, "mesh-rt-sim: pthread_create: %s\n", strerror(r));
                abort();
            }
            t->started = true;
        }
    }
    pthread_attr_destroy(&attr);
    return l;
}

void mesh_sim_join(mesh_sim_launch *l) {
    if (!l) return;
    for (size_t i = 0; i < (size_t)l->nodes * l->threads; i++)
        if (l->thr[i].started) pthread_join(l->thr[i].thread, NULL);
    free_launch(l);
}

#else
/* nvptx64: nothing here (an empty translation unit is not ISO C). */
typedef int mesh_rt_sim_unused;
#endif
