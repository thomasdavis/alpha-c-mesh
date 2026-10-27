/* native.rs's Worker (an image: at most one process at a time), the
 * executor (NativeBackend) and its compiler (NativeCompiler). */
#include "native_internal.h"

#include "mesh/loader.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/fnv.h"
#include "mesh/abi/host_status.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

bool mesh_native_check_format(const char *name, const char *path, const uint8_t *b, size_t n, char **err) {
#if defined(__aarch64__)
    const unsigned want = 183;
#elif defined(__x86_64__)
    const unsigned want = 62;
#else
    const unsigned want = 0;
#endif
    if (n < 20 || memcmp(b, "\x7f" "ELF", 4) != 0)
        return mesh_errf(err, "load: %s: %s is no ELF file, not an executable for %s", name, path, mesh_native_target());
    unsigned machine = (unsigned)b[18] | (unsigned)b[19] << 8;
    if (machine != want)
        return mesh_errf(err, "load: %s: %s is an ELF for machine %u, not an executable for %s", name, path, machine, mesh_native_target());
    return true;
}

/* ---- the image -------------------------------------------------------------------------- */

/* A spawn in progress (a start after a stop): its result, for poll to collect. */
typedef struct {
    atomic_int refs;
    pthread_mutex_t mu;
    bool done, abandoned;
    proc *p;
    char *err;
} pending;

static void pending_unref(pending *x) {
    if (!x || atomic_fetch_sub(&x->refs, 1) != 1) return;
    if (x->p) proc_unref(x->p);
    free(x->err);
    pthread_mutex_destroy(&x->mu);
    free(x);
}

static void spec_unref(spec *s) {
    if (!s || atomic_fetch_sub(&s->refs, 1) != 1) return;
    free(s->name);
    free(s->bin);
    free(s);
}

typedef struct {
    proc *p;
    pending *pend;
    uint32_t gen, slot;
    bool active; /* launched, not yet halted or gone */
    bool exit_seen;
    uint64_t exit_seen_at;
    bool dead_seen;
    uint64_t dead_seen_at; /* when its process was first seen gone (its status pipe drains after) */
    char *error;           /* a failure for `crashed` to report once */
    uint32_t last_exit;
    uint64_t cpu_gone; /* CPU ns of this image's processes that are gone */
} run_state;

struct mesh_native_worker {
    char *name;
    uint64_t bin_hash, bin_bytes;
    spec *spec;
    mesh_native_host *host;
    pthread_mutex_t mu;
    run_state run;
    _Atomic uint32_t crashes;
};

const char *mesh_native_worker_name(const mesh_native_worker *w) { return w->name; }
const char *mesh_native_worker_bin(const mesh_native_worker *w) { return w->spec->bin; }
uint64_t mesh_native_worker_bin_hash(const mesh_native_worker *w) { return w->bin_hash; }
uint64_t mesh_native_worker_bin_bytes(const mesh_native_worker *w) { return w->bin_bytes; }

bool mesh_native_worker_warm(mesh_native_worker *w, char **err) {
    pthread_mutex_lock(&w->mu);
    bool ok = true;
    if (!(w->run.p && !proc_exited(w->run.p, NULL))) {
        proc *p;
        ok = host_spawn(w->host, w->spec, &p, err);
        if (ok) {
            if (w->run.p) proc_unref(w->run.p);
            w->run.p = p;
        }
    }
    pthread_mutex_unlock(&w->mu);
    return ok;
}

/* A process of this image is gone: its CPU time kept, the registry updated. */
static void gone(mesh_native_worker *w, run_state *r, proc *p, bool crashed) {
    uint64_t cpu, rss;
    float pct;
    proc_last(p, &cpu, &rss, &pct);
    r->cpu_gone += cpu;
    int st;
    if (proc_exited(p, &st)) r->last_exit = mesh_exit_word(st);
    if (crashed) {
        atomic_fetch_add(&w->crashes, 1);
        atomic_fetch_add(&w->host->crashes, 1);
        atomic_fetch_add(&mesh_cpuuse_worker_crashes, 1);
    }
    host_forget(w->host, p);
    if (r->p == p) r->p = NULL;
    proc_unref(p);
}

/* Advances the launch's state: a finished spawn, a halt, an exit, a death. */
static void poll_run(mesh_native_worker *w) {
    run_state *r = &w->run;
    if (r->pend) {
        pending *x = r->pend;
        pthread_mutex_lock(&x->mu);
        bool done = x->done;
        proc *p = x->p;
        char *e = x->err;
        x->p = NULL;
        x->err = NULL;
        pthread_mutex_unlock(&x->mu);
        if (!done) return;
        r->pend = NULL;
        pending_unref(x);
        if (p) r->p = p;
        else {
            r->active = false;
            free(r->error);
            r->error = e;
            e = NULL;
        }
        free(e);
    }
    if (!r->active) return;
    proc *p = r->p;
    if (!p) {
        r->active = false;
        return;
    }
    size_t ctl = mesh_prog_ctl(r->slot);
    bool exit_mode = mesh_mailbox_get(w->host->mb, ctl + MESH_P_SHUTDOWN) == MESH_SHUTDOWN_EXIT;
    int st;
    if (proc_exited(p, &st)) {
        /* Its last status lines and output may still be in the pipes: judge once they
         * are drained (at most 500 ms: a descendant may hold them open). */
        bool drained = atomic_load(&p->eof) && atomic_load(&p->outs_done) >= 2;
        if (!drained) {
            if (!r->dead_seen) {
                r->dead_seen = true;
                r->dead_seen_at = mesh_native_now_ns();
            }
            if (mesh_native_now_ns() - r->dead_seen_at < 500000000u) return;
        }
        bool halted = atomic_load(&p->halted_gen) == r->gen;
        r->active = false;
        /* Leaving after a halt it was told to exit at is the normal stop; anything else is a crash. */
        bool clean = exit_mode && halted && mesh_status_success(st);
        if (!clean) {
            char *d = mesh_describe_status(st), *tail = proc_tail_text(p);
            free(r->error);
            r->error = mesh_fmt("worker %d exited (%s)%s; output: %s", (int)p->pid, d, exit_mode ? " while stopping" : " while running", tail);
            free(d);
            free(tail);
        }
        gone(w, r, p, !clean);
        return;
    }
    bool halted = atomic_load(&p->halted_gen) == r->gen;
    if (exit_mode) {
        if (!r->exit_seen) {
            r->exit_seen = true;
            r->exit_seen_at = mesh_native_now_ns();
        }
        if (halted) proc_tell_exit(p);
        uint64_t el = mesh_native_now_ns() - r->exit_seen_at;
        if (el > ((uint64_t)MESH_NATIVE_EXIT_GRACE_MS + 2000u) * 1000000u) {
            mesh_error("native: %s worker %d did not halt and exit in %.3fs; killing it", w->name, (int)p->pid, (double)el / 1e9);
            proc_kill(p);
        }
    } else if (halted) {
        /* Halted (a blink): it stays, gated, for the next `run`. */
        r->active = false;
    }
}

typedef struct {
    mesh_native_host *host;
    spec *spec;
    char *line;
    pending *pend;
} spawn_job;

static void *spawn_thread(void *a) {
    spawn_job *j = a;
    proc *p = NULL;
    char *err = NULL;
    if (host_spawn(j->host, j->spec, &p, &err) && !proc_send(p, j->line)) {
        proc_kill(p);
        host_forget(j->host, p);
        proc_unref(p);
        p = NULL;
        err = mesh_fmt("%s: the new worker took no `run`", j->spec->name);
    }
    pthread_mutex_lock(&j->pend->mu);
    if (j->pend->abandoned && p) {
        /* Released while it started: nobody will run it. */
        proc_tell_exit(p);
        pthread_mutex_unlock(&j->pend->mu);
        proc_retire(p, j->host);
        pthread_mutex_lock(&j->pend->mu);
        p = NULL;
    }
    j->pend->p = p;
    j->pend->err = err;
    j->pend->done = true;
    pthread_mutex_unlock(&j->pend->mu);
    pending_unref(j->pend);
    spec_unref(j->spec);
    free(j->line);
    free(j);
    return NULL;
}

bool mesh_native_worker_launch(mesh_native_worker *w, uint32_t slot, char **err) {
    pthread_mutex_lock(&w->mu);
    run_state *r = &w->run;
    poll_run(w);
    if (r->active || r->pend) {
        pthread_mutex_unlock(&w->mu);
        return mesh_errf(err, "%s: its worker still runs a launch", w->name);
    }
    r->gen++;
    r->slot = slot;
    r->exit_seen = r->dead_seen = false;
    free(r->error);
    r->error = NULL;
    char *line = mesh_fmt("run %u %u", (unsigned)r->gen, (unsigned)slot);
    if (r->p) {
        proc *p = r->p;
        if (!proc_exited(p, NULL) && proc_send(p, line)) {
            r->active = true;
            free(line);
            pthread_mutex_unlock(&w->mu);
            return true;
        }
        gone(w, r, p, false);
    }
    pending *x = mesh_alloc(sizeof *x);
    atomic_init(&x->refs, 2);
    pthread_mutex_init(&x->mu, NULL);
    spawn_job *j = mesh_alloc(sizeof *j);
    atomic_fetch_add(&w->spec->refs, 1);
    *j = (spawn_job){w->host, w->spec, line, x};
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    bool started = pthread_create(&t, &at, spawn_thread, j) == 0;
    pthread_attr_destroy(&at);
    if (!started) {
        pending_unref(x);
        pending_unref(x);
        spec_unref(w->spec);
        free(line);
        free(j);
        pthread_mutex_unlock(&w->mu);
        return mesh_errf(err, "native: thread: cannot start one");
    }
    r->pend = x;
    r->active = true;
    pthread_mutex_unlock(&w->mu);
    return true;
}

bool mesh_native_worker_query(mesh_native_worker *w) {
    pthread_mutex_lock(&w->mu);
    poll_run(w);
    bool over = !w->run.active && !w->run.pend;
    pthread_mutex_unlock(&w->mu);
    return over;
}

char *mesh_native_worker_crashed(mesh_native_worker *w) {
    pthread_mutex_lock(&w->mu);
    poll_run(w);
    char *e = w->run.error;
    w->run.error = NULL;
    pthread_mutex_unlock(&w->mu);
    return e;
}

mesh_native_stats mesh_native_worker_stats(mesh_native_worker *w) {
    pthread_mutex_lock(&w->mu);
    poll_run(w);
    proc *live = w->run.p && !proc_exited(w->run.p, NULL) ? w->run.p : NULL;
    uint64_t cpu = 0, rss = 0;
    float pct = 0;
    if (live) proc_last(live, &cpu, &rss, &pct);
    mesh_native_stats s = {live ? live->pid : 0, pct, rss, w->run.cpu_gone + cpu, atomic_load(&w->spec->spawns), atomic_load(&w->crashes),
                           w->run.last_exit, live ? live->limits : 0};
    pthread_mutex_unlock(&w->mu);
    return s;
}

pid_t mesh_native_worker_pid(mesh_native_worker *w) {
    pthread_mutex_lock(&w->mu);
    pid_t pid = w->run.p && !proc_exited(w->run.p, NULL) ? w->run.p->pid : 0;
    pthread_mutex_unlock(&w->mu);
    return pid;
}

void mesh_native_worker_release(mesh_native_worker *w) {
    pthread_mutex_lock(&w->mu);
    if (w->run.p) {
        proc *p = w->run.p;
        w->run.p = NULL;
        proc_retire(p, w->host);
    }
    if (w->run.pend) {
        pthread_mutex_lock(&w->run.pend->mu);
        w->run.pend->abandoned = true;
        proc *p = w->run.pend->p;
        w->run.pend->p = NULL;
        pthread_mutex_unlock(&w->run.pend->mu);
        if (p) proc_retire(p, w->host);
        pending_unref(w->run.pend);
        w->run.pend = NULL;
    }
    w->run.active = false;
    pthread_mutex_unlock(&w->mu);
}

static void worker_free(mesh_native_worker *w) {
    mesh_native_worker_release(w);
    free(w->run.error);
    spec_unref(w->spec);
    free(w->name);
    pthread_mutex_destroy(&w->mu);
    free(w);
}

/* ---- the image as a mesh_kernel ------------------------------------------------------------ */

static bool k_zero(const mesh_kernel *k, uint32_t *out, char **err) {
    (void)k, (void)err;
    *out = 0;
    return true;
}
static bool k_one(const mesh_kernel *k, uint32_t *out, char **err) {
    (void)k, (void)err;
    *out = 1; /* a process: nothing resident */
    return true;
}
static uint64_t k_scratch(const mesh_kernel *k) {
    (void)k;
    return 0;
}
static bool k_global(const mesh_kernel *k, const char *name, uint32_t *out, char **err) {
    (void)k;
    /* The manifest's abi was checked, and the worker's native ABI at its `ready`. */
    if (strcmp(name, MESH_RT_ABI_SYMBOL) == 0) {
        *out = MESH_RT_ABI;
        return true;
    }
    return mesh_errf(err, "global %s: a native image reports only %s", name, MESH_RT_ABI_SYMBOL);
}
static void k_drop(mesh_kernel *k) { worker_free(k->self); }

static const mesh_kernel_ops kernel_ops = {MESH_BACKEND_NATIVE, k_zero, k_zero, k_one, k_scratch, k_global, k_drop};

/* ---- the compiler --------------------------------------------------------------------------- */

typedef struct {
    mesh_native_host *host;
    mesh_compiler comp;
    mesh_caps caps;
} native_backend;

static bool c_needs_ptx(const mesh_compiler *c) {
    (void)c;
    return false;
}

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    mesh_text t = {0};
    char buf[65536];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) mesh_text_putn(&t, buf, k);
    fclose(f);
    *n = t.len;
    if (!t.data) {
        t.data = mesh_alloc(1);
        return t.data;
    }
    return mesh_text_take(&t);
}

static bool c_prepare(const mesh_compiler *c, mesh_payload *src, const uint64_t *expect, const char *root, mesh_prepared *out, char **err) {
    native_backend *nb = c->self;
    uint64_t t0 = mesh_native_now_ns();
    *out = (mesh_prepared){0};
    mesh_manifest m;
    if (!mesh_check_manifest(src, MESH_BACKEND_NATIVE, &m, err)) {
        mesh_payload_free(src);
        return false;
    }
    char *dir = mesh_loader_resolve(root, src->dir, err), *bin = NULL, *source = NULL;
    uint8_t *code = NULL;
    size_t code_len = 0;
    if (!dir) goto fail;
    bin = mesh_native_binary(dir, m.name);
    code = (uint8_t *)slurp(bin, &code_len);
    if (!code) {
        char *e = os_error(errno);
        mesh_errf(err, "unsupported: %s has no binary for %s (%s: %s)", m.name, mesh_native_target(), bin, e);
        free(e);
        goto fail;
    }
    if (!mesh_native_check_format(m.name, bin, code, code_len, err)) goto fail;
    struct stat st;
    if (stat(bin, &st) != 0) {
        char *e = os_error(errno);
        mesh_errf(err, "load: %s: %s", bin, e);
        free(e);
        goto fail;
    }
    if ((st.st_mode & 0111) == 0) {
        mesh_errf(err, "load: %s: %s is not executable", m.name, bin);
        goto fail;
    }
    char *sp = mesh_fmt("%s/source.hash", dir);
    size_t sn;
    source = slurp(sp, &sn);
    if (!source) {
        char *e = os_error(errno);
        mesh_errf(err, "manifest: %s: %s/source.hash: %s (build it with mesh-build-programs)", m.name, dir, e);
        free(e);
        free(sp);
        goto fail;
    }
    free(sp);
    uint64_t hash = mesh_native_identity(src->manifest, source, m.param);
    if (!mesh_check_hash(m.name, "source", hash, expect, err)) goto fail;
    mesh_native_limits limits = mesh_native_limits_of(&m);
    if (!mesh_native_limits_available(&limits, err)) goto fail;
    uint64_t bin_hash = mesh_fnv1a64(code, code_len);
    if (m.device_gpu) {
        if (nb->host->cfg.gpu == MESH_GPU_ACCESS_NONE) {
            mesh_errf(err, "unsupported: %s: device gpu, and this meshd has no GPU (--backend wasm)", m.name);
            goto fail;
        }
        if (nb->host->cfg.gpu == MESH_GPU_ACCESS_BLINK) {
            mesh_errf(err, "load: %s: device gpu needs --isolation mps (a worker's own CUDA context is time-sliced against the resident kernels)", m.name);
            goto fail;
        }
    }
    spec *s = mesh_alloc(sizeof *s);
    atomic_init(&s->refs, 1);
    s->name = mesh_strdup(m.name);
    s->bin = bin;
    bin = NULL;
    s->limits = limits;
    s->gpu = m.device_gpu;
    mesh_native_worker *w = mesh_alloc(sizeof *w);
    w->name = mesh_strdup(m.name);
    w->bin_hash = bin_hash;
    w->bin_bytes = code_len;
    w->spec = s;
    w->host = nb->host;
    pthread_mutex_init(&w->mu, NULL);
    const char *trimmed = source;
    size_t tn = strlen(source);
    mesh_trim(&trimmed, &tn);
    mesh_info("native: %s checked: %zu bytes for %s, binary %016llx, image %016llx (source %.*s)", m.name, code_len, mesh_native_target(),
              (unsigned long long)bin_hash, (unsigned long long)hash, (int)tn, trimmed);
    mesh_kernel *k = mesh_alloc(sizeof *k);
    *k = (mesh_kernel){&kernel_ops, w};
    out->manifest = m;
    out->hash = hash;
    out->code = code;
    out->code_len = code_len;
    out->compile_ms = (double)(mesh_native_now_ns() - t0) / 1e6;
    out->src = *src;
    *src = (mesh_payload){0};
    out->kernel = k;
    out->exec = MESH_BACKEND_NATIVE;
    out->reg_cap = 0;
    free(dir);
    free(source);
    return true;
fail:
    free(dir);
    free(bin);
    free(code);
    free(source);
    mesh_manifest_free(&m);
    mesh_payload_free(src);
    return false;
}

static const mesh_compiler_ops compiler_ops = {c_needs_ptx, c_prepare};

/* ---- the backend ---------------------------------------------------------------------------- */

static native_backend *nbk(const mesh_backend *b) { return b->self; }
static mesh_native_worker *worker_of(const mesh_kernel *k) { return mesh_kernel_native(k); }

static const mesh_caps *b_caps(const mesh_backend *b) { return &nbk(b)->caps; }
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
    *host = nbk(b)->host->shm.base;
    *dev = 0;
    return true;
}
static bool b_make_current(const mesh_backend *b, char **err) {
    (void)b, (void)err;
    return true;
}
static const mesh_compiler *b_compiler(const mesh_backend *b) { return &nbk(b)->comp; }
static bool b_load(const mesh_backend *b, mesh_prepared *p, mesh_kernel **out, char **err) {
    (void)b;
    if (!p->kernel) return mesh_errf(err, "internal: a native image not checked on the JIT thread");
    *out = p->kernel;
    p->kernel = NULL;
    return true;
}
static bool b_new_stream(const mesh_backend *b, uint32_t slot, const mesh_kernel *k, mesh_stream **out, char **err) {
    (void)b, (void)k, (void)err;
    mesh_stream *s = mesh_alloc(sizeof *s);
    *s = (mesh_stream){MESH_BACKEND_NATIVE, slot, NULL, NULL};
    *out = s;
    return true;
}
static bool b_launch(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, uint32_t nodes, uint64_t dev, uint32_t slot, char **err) {
    (void)b, (void)st, (void)nodes, (void)dev;
    mesh_native_worker *w = worker_of(k);
    if (!w) return mesh_errf(err, "internal: a non-native image on the native executor");
    return mesh_native_worker_launch(w, slot, err);
}
static bool b_query(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, bool *done, char **err) {
    (void)b, (void)st;
    mesh_native_worker *w = worker_of(k);
    if (!w) return mesh_errf(err, "internal: a non-native image on the native executor");
    *done = mesh_native_worker_query(w);
    return true;
}
static bool b_unload(const mesh_backend *b, mesh_kernel *k, char **err) {
    (void)b;
    if (!worker_of(k)) {
        mesh_kernel_drop(k);
        return mesh_errf(err, "internal: a non-native image on the native executor");
    }
    mesh_kernel_drop(k); /* releases: its process exits, reaped off this thread */
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
static char *b_crashed(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k) {
    (void)b, (void)st;
    mesh_native_worker *w = worker_of(k);
    return w ? mesh_native_worker_crashed(w) : NULL;
}
static uint32_t b_online(const mesh_backend *b) {
    (void)b;
    return MESH_NATIVE_ONLINE_MS;
}
static void b_sample(const mesh_backend *b) {
    mesh_native_totals t = mesh_native_host_totals(nbk(b)->host);
    atomic_store(&mesh_cpuuse_workers, t.procs);
}
static void b_destroy(mesh_backend *b) {
    native_backend *n = nbk(b);
    mesh_caps_free(&n->caps);
    free(n);
    free(b);
}

static const mesh_backend_ops backend_ops = {
    b_caps, b_true, b_false, b_isolation, b_mailbox, b_make_current, b_compiler, b_load, b_new_stream, b_launch, b_query, b_unload,
    b_stack_limit, b_set_stack_limit, b_module_scope, b_crashed, b_true, b_true, b_sample, b_online, b_destroy,
};

mesh_backend *mesh_native_backend_new(const mesh_shm *shm, const char *logs, const mesh_worker_cfg *cfg) {
    native_backend *n = mesh_alloc(sizeof *n);
    n->host = mesh_native_host_new(shm, logs, cfg);
    n->comp = (mesh_compiler){&compiler_ops, n};
    long threads = sysconf(_SC_NPROCESSORS_ONLN);
    if (threads < 1) threads = 1;
    n->caps = (mesh_caps){MESH_BACKEND_NATIVE, mesh_fmt("native workers (%s)", mesh_native_target()), 0, 0, (uint32_t)threads,
                          (uint32_t)threads, false, 0, 0, 0, 0, mesh_strdup(mesh_native_target())};
    mesh_backend *b = mesh_alloc(sizeof *b);
    *b = (mesh_backend){&backend_ops, n};
    mesh_backend_on_cleanup(MESH_CLEANUP_NATIVE, mesh_native_cleanup);
    return b;
}

mesh_native_host *mesh_native_backend_host(const mesh_backend *b) { return nbk(b)->host; }
