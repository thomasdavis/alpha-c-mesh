/* The backend and its lanes (mesh-host/src/wasm.rs `WasmBackend`, `Lane`, `trap_fault`). */
#include "mesh/exec_wasm.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/fault.h"
#include "mesh/abi/host_status.h"
#include "mesh/abi/ops.h"
#include "mesh/cpuuse.h"
#include "mesh/lease.h"
#include "mesh/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "exec_internal.h"
#include "rt_internal.h"

uint32_t mesh_wasm_trap_fault(int trap) {
    if (trap == WASM_TRAP_INTERRUPT) return MESH_FAULT_TIME;
    if (trap >= 0) return MESH_FAULT_TRAP | ((uint32_t)trap + 1u) << 8;
    return MESH_FAULT_TRAP;
}

/* ---- a lane: one node of a running slot, the lease's runner ---- */

typedef struct {
    mesh_wasm_program *p;
    wasm_instance *inst;
    mesh_wasm_lane_data data;
    uint32_t init, task;
    uint32_t nodes;
    mesh_meter meter;
    bool metered;
    uint64_t ticked;
    mesh_lease lease;
    mesh_lane_stats stats;
} lane;

typedef struct {
    _Atomic bool done;
    pthread_t th;
    bool started;
} handle;

typedef struct {
    handle *h;
    uint32_t n;
} slot_lanes;

typedef struct {
    mesh_wasm_rt *rt;
    mesh_mailbox mb;
    mesh_caps caps;
    mesh_compiler comp;
    pthread_mutex_t mu;
    slot_lanes lanes[MESH_PROG_SLOTS];
} backend;

static size_t lane_max_take(void *self) {
    (void)self;
    return 1;
}

static void lane_heartbeat(void *self) {
    lane *l = self;
    uint64_t mem = 0;
    if (l->inst) wasm_memory(l->inst, &mem);
    if (l->metered) mesh_meter_tick(&l->meter, mem);
}

static bool lane_run(void *self, mesh_lease *lease, const mesh_taken *taken, size_t n) {
    lane *l = self;
    for (size_t i = 0; i < n; i++) {
        const mesh_taken *k = &taken[i];
        if (!l->inst) {
            /* Its re-instantiation failed after a trap: nothing to run on. */
            mesh_lease_fault(lease, MESH_FAULT_TRAP);
            return false;
        }
        mesh_wasm_lane_data_task(&l->data, k->t);
        wasm_set_deadline(l->inst, atomic_load(&l->p->rt->epoch) + l->p->task_ms);
        uint64_t io[2] = {k->op, k->arg};
        uint64_t t0 = mesh_host_ns();
        int trap = k->op == MESH_OP_INIT ? wasm_call(l->inst, l->init, io) : wasm_call(l->inst, l->task, io);
        uint64_t t1 = mesh_host_ns();
        if (trap == WASM_NUM_OK) {
            mesh_lease_complete(lease, lease->first, k, k->op == MESH_OP_INIT ? MESH_R_NONE : (uint32_t)io[0], t0, t1);
            continue;
        }
        uint32_t code = mesh_wasm_trap_fault(trap);
        mesh_lease_fault(lease, code);
        if (code == MESH_FAULT_TIME)
            mesh_error("wasm %s slot %u node %u: ticket %u op %u ran past task_ms = %u ms (interrupted): wasm trap: %s", l->p->name,
                       lease->slot, lease->first, k->t, k->op, l->p->task_ms, wasm_trap_message(trap));
        else
            mesh_error("wasm %s slot %u node %u: ticket %u op %u trapped: wasm trap: %s", l->p->name, lease->slot, lease->first,
                       k->t, k->op, wasm_trap_message(trap));
        /* A fresh instance (its memory may be anywhere mid-task); the driver fails the slot. */
        char *err = NULL;
        wasm_instance_free(l->inst);
        l->inst = mesh_wasm_instantiate(l->p, lease->mb, lease->slot, lease->first, l->nodes, &l->data, &err);
        if (!l->inst || !mesh_wasm_entry_points(l->p, l->inst, &l->init, &l->task, &err))
            mesh_error("wasm %s: re-instantiate after the trap: %s", l->p->name, err ? err : "?");
        free(err);
        return false;
    }
    /* A busy lane accounts its CPU time about every ms (a thread-CPU read costs a syscall). */
    if (mesh_host_ns() - l->ticked > 1000000) {
        l->ticked = mesh_host_ns();
        lane_heartbeat(l);
    }
    return true;
}

static const mesh_runner RUNNER = {lane_max_take, lane_run, lane_heartbeat};

static void *lane_main(void *arg) {
    lane *l = arg;
    mesh_wasm_rt *rt = l->p->rt;
    /* This thread's CPU time and the instance's memory count for the slot (PL_HOST). */
    mesh_meter_start(&l->meter, l->lease.slot);
    l->metered = true;
    lane_heartbeat(l);
    if (l->inst) mesh_lease_run(&l->lease, &RUNNER, l);
    else atomic_store(l->lease.done, true);
    atomic_fetch_sub(&rt->lanes, 1);
    mesh_meter_end(&l->meter);
    wasm_instance_free(l->inst);
    mesh_wasm_program_free(l->p);
    free(l);
    return NULL;
}

/* ---- the backend ---- */

static const mesh_caps *b_caps(const mesh_backend *b) { return &((backend *)b->self)->caps; }
static bool b_true(const mesh_backend *b) {
    (void)b;
    return true;
}
static bool b_false(const mesh_backend *b) {
    (void)b;
    return false;
}
static uint32_t b_isolation(const mesh_backend *b) {
    (void)b;
    return MESH_ISOLATION_LEASE;
}
static bool b_mailbox(const mesh_backend *b, uint8_t **host, uint64_t *dev, char **err) {
    (void)err;
    backend *x = b->self;
    *host = x->mb.base;
    *dev = x->mb.dev;
    return true;
}
static bool b_make_current(const mesh_backend *b, char **err) {
    (void)b, (void)err;
    return true;
}
static const mesh_compiler *b_compiler(const mesh_backend *b) { return &((backend *)b->self)->comp; }

static bool b_load(const mesh_backend *b, mesh_prepared *p, mesh_kernel **out, char **err) {
    (void)b;
    if (!p->kernel) {
        if (err) *err = strdup("internal: a wasm image not compiled on the JIT thread");
        return false;
    }
    *out = p->kernel;
    p->kernel = NULL;
    return true;
}

static bool b_new_stream(const mesh_backend *b, uint32_t slot, const mesh_kernel *k, mesh_stream **out, char **err) {
    (void)b, (void)k;
    mesh_stream *s = calloc(1, sizeof *s);
    if (!s) {
        if (err) *err = strdup("out of memory");
        return false;
    }
    s->exec = MESH_BACKEND_WASM;
    s->slot = slot;
    *out = s;
    return true;
}

static bool running(const slot_lanes *s) {
    for (uint32_t i = 0; s->h && i < s->n; i++)
        if (!atomic_load(&s->h[i].done)) return true;
    return false;
}

/* Joins and frees a slot's handles (all done). */
static void reap(slot_lanes *s) {
    for (uint32_t i = 0; s->h && i < s->n; i++)
        if (s->h[i].started) pthread_join(s->h[i].th, NULL);
    free(s->h);
    s->h = NULL;
    s->n = 0;
}

/* One lane per node, each instantiated here (a failure fails the launch). */
static bool b_launch(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, uint32_t nodes, uint64_t dev,
                     uint32_t slot, char **err) {
    (void)dev;
    backend *x = b->self;
    if (st->exec != MESH_BACKEND_WASM) {
        if (err) *err = strdup("internal: a GPU stream on the wasm executor");
        return false;
    }
    if (st->slot != slot) {
        if (err) *err = mesh_wasm_fmt("internal: slot %u launched on lanes %u", slot, st->slot);
        return false;
    }
    mesh_wasm_program *p = (mesh_wasm_program *)mesh_wasm_kernel_program(k);
    if (!p) {
        if (err) *err = strdup("internal: a GPU image on the wasm executor");
        return false;
    }
    pthread_mutex_lock(&x->mu);
    slot_lanes *sl = &x->lanes[slot];
    if (running(sl)) {
        pthread_mutex_unlock(&x->mu);
        if (err) *err = mesh_wasm_fmt("slot %u: its lanes still run", slot);
        return false;
    }
    reap(sl);
    lane **ls = calloc(nodes ? nodes : 1, sizeof *ls);
    handle *hs = calloc(nodes ? nodes : 1, sizeof *hs);
    bool ok = ls && hs;
    for (uint32_t node = 0; ok && node < nodes; node++) {
        lane *l = calloc(1, sizeof *l);
        if (!l) {
            ok = false;
            break;
        }
        ls[node] = l;
        l->p = p;
        l->nodes = nodes;
        if (!(l->inst = mesh_wasm_instantiate(p, x->mb, slot, node, nodes, &l->data, err)) ||
            !mesh_wasm_entry_points(p, l->inst, &l->init, &l->task, err))
            ok = false;
    }
    if (!ok) {
        for (uint32_t i = 0; ls && i < nodes; i++)
            if (ls[i]) {
                wasm_instance_free(ls[i]->inst);
                free(ls[i]);
            }
        free(ls);
        free(hs);
        pthread_mutex_unlock(&x->mu);
        if (err && !*err) *err = strdup("out of memory");
        return false;
    }
    for (uint32_t node = 0; node < nodes; node++) {
        lane *l = ls[node];
        atomic_init(&hs[node].done, false);
        mesh_lease_init(&l->lease, x->mb, slot, node, 1, false, &hs[node].done, &l->stats);
        atomic_fetch_add(&p->refs, 1);
        atomic_fetch_add(&x->rt->lanes, 1);
        if (pthread_create(&hs[node].th, NULL, lane_main, l) != 0) {
            atomic_fetch_sub(&x->rt->lanes, 1);
            atomic_store(&hs[node].done, true);
            wasm_instance_free(l->inst);
            mesh_wasm_program_free(p);
            free(l);
            /* The lanes already started run on; the launch fails. */
            for (uint32_t k2 = node + 1; k2 < nodes; k2++) {
                wasm_instance_free(ls[k2]->inst);
                free(ls[k2]);
                atomic_store(&hs[k2].done, true);
            }
            sl->h = hs;
            sl->n = nodes;
            free(ls);
            pthread_mutex_unlock(&x->mu);
            if (err) *err = strdup("wasm lane thread: cannot start it");
            return false;
        }
        hs[node].started = true;
    }
    free(ls);
    sl->h = hs;
    sl->n = nodes;
    pthread_mutex_unlock(&x->mu);
    return true;
}

static bool b_query(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, bool *done, char **err) {
    (void)k;
    backend *x = b->self;
    if (st->exec != MESH_BACKEND_WASM) {
        if (err) *err = strdup("internal: a GPU stream on the wasm executor");
        return false;
    }
    pthread_mutex_lock(&x->mu);
    slot_lanes *sl = &x->lanes[st->slot];
    if (!sl->h) *done = true;
    else if (running(sl)) *done = false;
    else {
        reap(sl);
        *done = true;
    }
    pthread_mutex_unlock(&x->mu);
    return true;
}

static bool b_unload(const mesh_backend *b, mesh_kernel *k, char **err) {
    (void)b, (void)err;
    mesh_kernel_drop(k);
    return true;
}

static bool b_stack_limit(const mesh_backend *b, size_t *out, char **err) {
    (void)b, (void)err;
    *out = SIZE_MAX;
    return true;
}

static bool b_set_stack_limit(const mesh_backend *b, size_t bytes, char **err) {
    (void)b, (void)bytes, (void)err;
    return true;
}

static bool b_module_scope(const mesh_backend *b, bool has_slot, uint32_t slot, uint32_t *scope) {
    (void)b;
    *scope = has_slot ? slot : MESH_NO_SLOT;
    return true;
}

static void b_destroy(mesh_backend *b) {
    backend *x = b->self;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        if (x->lanes[s].h && running(&x->lanes[s])) {
            /* Lanes still running at exit: ask them to halt and wait. */
            mesh_mailbox_publish(x->mb, mesh_prog_ctl(s) + MESH_P_SHUTDOWN, 1);
        }
        reap(&x->lanes[s]);
    }
    mesh_wasm_rt_free(x->rt);
    mesh_caps_free(&x->caps);
    pthread_mutex_destroy(&x->mu);
    free(x);
    free(b);
}

static const mesh_backend_ops OPS = {
    .caps = b_caps,
    .isolated = b_true,
    .resident = b_false,
    .isolation = b_isolation,
    .mailbox = b_mailbox,
    .make_current = b_make_current,
    .compiler = b_compiler,
    .load = b_load,
    .new_stream = b_new_stream,
    .launch = b_launch,
    .query = b_query,
    .unload = b_unload,
    .stack_limit = b_stack_limit,
    .set_stack_limit = b_set_stack_limit,
    .module_scope = b_module_scope,
    .crashed = NULL,
    .owns_halt = NULL,
    .reads_live = b_true,
    .sample = NULL,
    .online_timeout_ms = NULL,
    .destroy = b_destroy,
};

#if defined(__x86_64__)
#define ARCH "x86_64"
#elif defined(__aarch64__)
#define ARCH "aarch64"
#else
#define ARCH "unknown"
#endif

bool mesh_wasm_backend_open(uint8_t *mailbox, const char *cache, mesh_backend **out, char **err) {
    mesh_wasm_rt *rt = mesh_wasm_rt_new(cache, err);
    if (!rt) return false;
    backend *x = calloc(1, sizeof *x);
    mesh_backend *b = calloc(1, sizeof *b);
    if (!x || !b) {
        free(x);
        free(b);
        mesh_wasm_rt_free(rt);
        if (err) *err = strdup("wasm: out of memory");
        return false;
    }
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    uint32_t threads = n > 0 ? (uint32_t)n : 1;
    x->rt = rt;
    x->mb = mesh_mailbox_new(mailbox, (uint64_t)(uintptr_t)mailbox);
    x->caps.kind = MESH_BACKEND_WASM;
    x->caps.name = mesh_wasm_fmt("%s CPU, %u threads (wasm-interp)", ARCH, threads);
    x->caps.sms_total = threads;
    x->caps.sms = threads;
    x->caps.arch = mesh_wasm_fmt("wasm32 on %s", ARCH);
    x->comp.ops = &mesh_wasm_compiler_ops;
    x->comp.self = rt;
    pthread_mutex_init(&x->mu, NULL);
    b->ops = &OPS;
    b->self = x;
    *out = b;
    return true;
}
