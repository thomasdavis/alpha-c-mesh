/* native.rs's Proc and Host: one worker process (its pipes, status, output
 * tail, exit status, samples), and the processes of one meshd. */
#include "native_internal.h"

#include "mesh/abi/control_block.h"
#include "mesh/abi/native.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

uint64_t mesh_native_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

void mesh_native_sleep_us(unsigned us) {
    struct timespec t = {(time_t)(us / 1000000), (long)(us % 1000000) * 1000L};
    nanosleep(&t, NULL);
}

void proc_ref(proc *p) { atomic_fetch_add(&p->refs, 1); }

void proc_unref(proc *p) {
    if (!p || atomic_fetch_sub(&p->refs, 1) != 1) return;
    if (p->stdin_fd >= 0) close(p->stdin_fd);
    for (size_t i = 0; i < p->tail_len; i++) free(p->tail[(p->tail_head + i) % MESH_NATIVE_TAIL]);
    free(p->name);
    pthread_mutex_destroy(&p->stdin_mu);
    pthread_mutex_destroy(&p->tail_mu);
    pthread_mutex_destroy(&p->status_mu);
    pthread_mutex_destroy(&p->sample_mu);
    free(p);
}

/* True when SIGPIPE is pending for this thread or the process. */
static bool sigpipe_pending(void) {
    sigset_t pending;
    return sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE) == 1;
}

/* A worker that has exited has closed its stdin, and writing to it raises SIGPIPE, which by
 * default kills the whole process. Rust ignores SIGPIPE from startup, so the Rust executor only
 * sees EPIPE; a library can't assume its host program does the same, so SIGPIPE is blocked
 * around the write and one raised by this write is consumed before it is unblocked. */
bool proc_send(proc *p, const char *line) {
    pthread_mutex_lock(&p->stdin_mu);
    bool ok = false;
    if (p->stdin_fd >= 0) {
        sigset_t pipe_set, old;
        sigemptyset(&pipe_set);
        sigaddset(&pipe_set, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &pipe_set, &old);
        bool was_pending = sigpipe_pending();
        char *l = mesh_fmt("%s\n", line);
        size_t n = strlen(l), off = 0;
        ok = true;
        while (off < n) {
            ssize_t k = write(p->stdin_fd, l + off, n - off);
            if (k > 0) off += (size_t)k;
            else if (k < 0 && errno == EINTR) continue;
            else {
                if (k < 0 && errno == EPIPE && !was_pending) {
                    struct timespec zero = {0, 0};
                    while (sigtimedwait(&pipe_set, NULL, &zero) < 0 && errno == EINTR) {}
                }
                ok = false;
                break;
            }
        }
        free(l);
        pthread_sigmask(SIG_SETMASK, &old, NULL);
    }
    pthread_mutex_unlock(&p->stdin_mu);
    return ok;
}

void proc_tell_exit(proc *p) {
    if (atomic_exchange(&p->exit_sent, true)) return;
    proc_send(p, "exit");
    pthread_mutex_lock(&p->stdin_mu);
    if (p->stdin_fd >= 0) close(p->stdin_fd);
    p->stdin_fd = -1;
    pthread_mutex_unlock(&p->stdin_mu);
}

bool proc_exited(proc *p, int *status) {
    pthread_mutex_lock(&p->status_mu);
    if (!p->exited) {
        int st = 0;
        if (waitpid(p->pid, &st, WNOHANG) == p->pid) {
            p->exited = true;
            p->status = st;
        }
    }
    bool e = p->exited;
    if (e && status) *status = p->status;
    pthread_mutex_unlock(&p->status_mu);
    return e;
}

void proc_kill(proc *p) {
    pthread_mutex_lock(&p->status_mu);
    if (p->exited) {
        pthread_mutex_unlock(&p->status_mu);
        return;
    }
    kill(p->pid, SIGKILL);
    uint64_t t0 = mesh_native_now_ns();
    for (;;) {
        int st = 0;
        pid_t r = waitpid(p->pid, &st, WNOHANG);
        if (r == p->pid) {
            p->exited = true;
            p->status = st;
            pthread_mutex_unlock(&p->status_mu);
            return;
        }
        if (r < 0 || mesh_native_now_ns() - t0 > KILL_REAP_MS * 1000000ull) break;
        mesh_native_sleep_us(1000);
    }
    pthread_mutex_unlock(&p->status_mu);
    mesh_error("native: %s worker %d not reaped %ums after SIGKILL; left behind", p->name, (int)p->pid, KILL_REAP_MS);
}

char *proc_tail_text(proc *p) {
    pthread_mutex_lock(&p->tail_mu);
    char *s;
    if (p->tail_len == 0) s = mesh_strdup("(no output)");
    else {
        mesh_text t = {0};
        for (size_t i = 0; i < p->tail_len; i++) {
            if (i) mesh_text_puts(&t, " | ");
            mesh_text_puts(&t, p->tail[(p->tail_head + i) % MESH_NATIVE_TAIL]);
        }
        s = mesh_text_take(&t);
    }
    pthread_mutex_unlock(&p->tail_mu);
    return s;
}

static void tail_push(proc *p, const char *l) {
    pthread_mutex_lock(&p->tail_mu);
    if (p->tail_len == MESH_NATIVE_TAIL) {
        free(p->tail[p->tail_head]);
        p->tail[p->tail_head] = mesh_strdup(l);
        p->tail_head = (p->tail_head + 1) % MESH_NATIVE_TAIL;
    } else {
        p->tail[(p->tail_head + p->tail_len++) % MESH_NATIVE_TAIL] = mesh_strdup(l);
    }
    pthread_mutex_unlock(&p->tail_mu);
}

void proc_sample(proc *p, uint64_t *cpu, uint64_t *rss, float *pct) {
    pthread_mutex_lock(&p->sample_mu);
    uint64_t c, r;
    if (!proc_exited(p, NULL) && mesh_proc_usage(p->pid, &c, &r)) {
        uint64_t now = mesh_native_now_ns();
        double dt = (double)(now - p->sample_t) / 1e9;
        float pc = dt > 0.05 ? (float)((double)(c > p->sample_cpu ? c - p->sample_cpu : 0) / 1e9 / dt * 100.0) : p->sample_pct;
        p->sample_t = now;
        p->sample_cpu = c;
        p->sample_rss = r;
        p->sample_pct = pc;
    }
    *cpu = p->sample_cpu;
    *rss = p->sample_rss;
    *pct = p->sample_pct;
    pthread_mutex_unlock(&p->sample_mu);
}

void proc_last(proc *p, uint64_t *cpu, uint64_t *rss, float *pct) {
    pthread_mutex_lock(&p->sample_mu);
    *cpu = p->sample_cpu;
    *rss = p->sample_rss;
    *pct = p->sample_pct;
    pthread_mutex_unlock(&p->sample_mu);
}

/* A status line from the worker: `ready <abi>` or `halted <gen>`. */
void mesh_native_status_line(proc *p, const char *l) {
    char w0[16];
    unsigned long v;
    char rest[8];
    int n = sscanf(l, "%15s %lu %7s", w0, &v, rest);
    if (n < 2 || v > UINT32_MAX) return;
    if (strcmp(w0, "ready") == 0) atomic_store(&p->ready, v > 1 ? (uint32_t)v : 1u);
    else if (strcmp(w0, "halted") == 0) atomic_store(&p->halted_gen, (uint32_t)v);
}

typedef struct {
    proc *p;
    int fd;
    FILE *log;
    pthread_mutex_t *log_mu;
} reader;

static void *status_reader(void *a) {
    reader *r = a;
    FILE *f = fdopen(r->fd, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while (f && (n = getline(&line, &cap, f)) > 0) {
        if (line[n - 1] == '\n') line[n - 1] = 0;
        mesh_native_status_line(r->p, line);
    }
    free(line);
    if (f) fclose(f);
    else close(r->fd);
    atomic_store(&r->p->eof, true);
    proc_unref(r->p);
    free(r);
    return NULL;
}

/* A shared, refcounted log file for a process's two output readers. */
typedef struct {
    atomic_int refs;
    FILE *f;
    pthread_mutex_t mu;
} logfile;

static void logfile_unref(logfile *l) {
    if (!l || atomic_fetch_sub(&l->refs, 1) != 1) return;
    if (l->f) fclose(l->f);
    pthread_mutex_destroy(&l->mu);
    free(l);
}

typedef struct {
    proc *p;
    int fd;
    logfile *log;
} out_reader;

static void *output_reader(void *a) {
    out_reader *r = a;
    FILE *f = fdopen(r->fd, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while (f && (n = getline(&line, &cap, f)) > 0) {
        if (line[n - 1] == '\n') line[--n] = 0;
        if (r->log && r->log->f) {
            char st[16];
            mesh_log_stamp(st);
            pthread_mutex_lock(&r->log->mu);
            fprintf(r->log->f, "%s [%d] %s\n", st, (int)r->p->pid, line);
            fflush(r->log->f);
            pthread_mutex_unlock(&r->log->mu);
        }
        mesh_info("%s[%d]: %s", r->p->name, (int)r->p->pid, line);
        tail_push(r->p, line);
    }
    free(line);
    if (f) fclose(f);
    else close(r->fd);
    atomic_fetch_add(&r->p->outs_done, 1);
    logfile_unref(r->log);
    proc_unref(r->p);
    free(r);
    return NULL;
}

static bool start_thread(void *(*fn)(void *), void *arg) {
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    bool ok = pthread_create(&t, &at, fn, arg) == 0;
    pthread_attr_destroy(&at);
    return ok;
}

/* ---- the host -------------------------------------------------------------------------- */

static pthread_mutex_t hosts_mu = PTHREAD_MUTEX_INITIALIZER;
static mesh_native_host **hosts;
static size_t nhosts;

mesh_native_host *mesh_native_host_new(const mesh_shm *shm, const char *logs, const mesh_worker_cfg *cfg) {
    mesh_native_host *h = mesh_alloc(sizeof *h);
    h->mb = mesh_mailbox_new(shm->base, 0);
    h->shm = *shm;
    h->logs = mesh_strdup(logs);
    h->cfg = *cfg;
    size_t n = 0;
    while (cfg->env && cfg->env[n]) n++;
    h->cfg.env = mesh_alloc((n + 1) * sizeof *h->cfg.env);
    for (size_t i = 0; i < n; i++) h->cfg.env[i] = mesh_strdup(cfg->env[i]);
    h->cfg.mps_pipe_dir = cfg->mps_pipe_dir ? mesh_strdup(cfg->mps_pipe_dir) : NULL;
    pthread_mutex_init(&h->mu, NULL);
    pthread_mutex_lock(&hosts_mu);
    hosts = mesh_realloc(hosts, (nhosts + 1) * sizeof *hosts);
    hosts[nhosts++] = h;
    pthread_mutex_unlock(&hosts_mu);
    return h;
}

void host_forget(mesh_native_host *h, proc *p) {
    pthread_mutex_lock(&h->mu);
    for (size_t i = 0; i < h->nprocs; i++)
        if (h->procs[i] == p) {
            h->procs[i] = h->procs[--h->nprocs];
            pthread_mutex_unlock(&h->mu);
            proc_unref(p);
            return;
        }
    pthread_mutex_unlock(&h->mu);
}

/* A snapshot of the host's processes, each with a reference. */
static proc **host_procs(mesh_native_host *h, size_t *n) {
    pthread_mutex_lock(&h->mu);
    proc **v = mesh_alloc((h->nprocs ? h->nprocs : 1) * sizeof *v);
    for (size_t i = 0; i < h->nprocs; i++) {
        v[i] = h->procs[i];
        proc_ref(v[i]);
    }
    *n = h->nprocs;
    pthread_mutex_unlock(&h->mu);
    return v;
}

void mesh_native_host_counts(mesh_native_host *h, uint32_t *live, uint32_t *spawns, uint32_t *crashes) {
    size_t n;
    proc **v = host_procs(h, &n);
    uint32_t l = 0;
    for (size_t i = 0; i < n; i++) {
        if (!proc_exited(v[i], NULL)) l++;
        proc_unref(v[i]);
    }
    free(v);
    *live = l;
    *spawns = atomic_load(&h->spawns);
    *crashes = atomic_load(&h->crashes);
}

mesh_native_totals mesh_native_host_totals(mesh_native_host *h) {
    size_t n;
    proc **v = host_procs(h, &n);
    mesh_native_totals t = {0, 0, 0, atomic_load(&h->spawns), atomic_load(&h->crashes)};
    for (size_t i = 0; i < n; i++) {
        if (!proc_exited(v[i], NULL)) {
            uint64_t c, r;
            float pct;
            proc_sample(v[i], &c, &r, &pct);
            t.procs++;
            t.cpu_pct += pct;
            t.rss += r;
        }
        proc_unref(v[i]);
    }
    free(v);
    return t;
}

typedef struct {
    proc *p;
    mesh_native_host *h;
} retiree;

static void *retire_thread(void *a) {
    retiree *r = a;
    uint64_t t0 = mesh_native_now_ns();
    while (!proc_exited(r->p, NULL)) {
        if (mesh_native_now_ns() - t0 > (uint64_t)MESH_NATIVE_EXIT_GRACE_MS * 1000000u) {
            mesh_error("native: %s worker %d did not exit in %us; killing it", r->p->name, (int)r->p->pid, MESH_NATIVE_EXIT_GRACE_MS / 1000);
            proc_kill(r->p);
            break;
        }
        mesh_native_sleep_us(2000);
    }
    host_forget(r->h, r->p);
    proc_unref(r->p);
    free(r);
    return NULL;
}

/* Sends `exit`, then reaps it off the calling thread (killed after EXIT_GRACE).  Takes p's reference. */
void proc_retire(proc *p, mesh_native_host *h) {
    proc_tell_exit(p);
    retiree *r = mesh_alloc(sizeof *r);
    r->p = p;
    r->h = h;
    if (!start_thread(retire_thread, r)) retire_thread(r);
}

static void close_all(const int *fds, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (fds[i] >= 0) close(fds[i]);
}

bool host_spawn(mesh_native_host *h, spec *s, proc **out, char **err) {
    uint64_t t0 = mesh_native_now_ns();
    static atomic_uint seq;
    unsigned n = atomic_fetch_add(&seq, 1);
    const mesh_native_limits *l = &s->limits;
    char *argv[24];
    char *owned[8];
    size_t na = 0, no = 0;
    if (mesh_native_limits_scoped(l)) {
        argv[na++] = "systemd-run";
        argv[na++] = "--user";
        argv[na++] = "--scope";
        argv[na++] = "-q";
        argv[na++] = owned[no++] = mesh_fmt("--unit=meshd-%d-%s-%u", (int)getpid(), s->name, n);
        if (l->cpu_pct > 0) {
            argv[na++] = "-p";
            argv[na++] = owned[no++] = mesh_fmt("CPUQuota=%u%%", (unsigned)l->cpu_pct);
        }
        if (l->mem > 0) {
            argv[na++] = "-p";
            argv[na++] = owned[no++] = mesh_fmt("MemoryMax=%llu", (unsigned long long)l->mem);
            argv[na++] = "-p";
            argv[na++] = "MemorySwapMax=0";
        }
        argv[na++] = "--";
    }
    if (l->nice > 0) {
        argv[na++] = "/usr/bin/nice";
        argv[na++] = "-n";
        argv[na++] = owned[no++] = mesh_fmt("%u", (unsigned)l->nice);
    }
    if (l->background) {
#if defined(__APPLE__)
        argv[na++] = "/usr/sbin/taskpolicy";
        argv[na++] = "-b";
#else
        argv[na++] = "/usr/bin/chrt";
        argv[na++] = "--idle";
        argv[na++] = "0";
#endif
    }
    argv[na++] = s->bin;
    argv[na] = NULL;
    size_t ne = 0;
    while (h->cfg.env && h->cfg.env[ne]) ne++;
    char **env = mesh_alloc((ne + 4) * sizeof *env);
    size_t k = 0;
    env[k++] = mesh_fmt("%s=%u", MESH_NATIVE_ENV_ABI, (unsigned)MESH_NATIVE_ABI);
    env[k++] = mesh_fmt("%s=%s", MESH_NATIVE_ENV_NAME, s->name);
    for (size_t i = 0; i < ne; i++) env[k++] = mesh_strdup(h->cfg.env[i]);
    if (s->gpu && h->cfg.gpu == MESH_GPU_ACCESS_MPS && h->cfg.mps_pipe_dir)
        env[k++] = mesh_fmt("CUDA_MPS_PIPE_DIRECTORY=%s", h->cfg.mps_pipe_dir);
    env[k] = NULL;
    int inp[2] = {-1, -1}, outp[2] = {-1, -1}, errp[2] = {-1, -1}, stp[2] = {-1, -1};
    bool ok = mesh_pipe_cloexec(inp, err) && mesh_pipe_cloexec(outp, err) && mesh_pipe_cloexec(errp, err) && mesh_pipe_cloexec(stp, err);
    pid_t pid = 0;
    uint64_t ts = mesh_native_now_ns();
    if (ok) {
        mesh_spawn_fd fds[] = {{inp[0], 0}, {outp[1], 1}, {errp[1], 2}, {stp[1], MESH_NATIVE_STATUS_FD}, {h->shm.fd, MESH_NATIVE_MAILBOX_FD}};
        char *why = NULL;
        ok = mesh_spawn(argv, env, fds, 5, &pid, &why);
        if (!ok) {
            mesh_errf(err, "load: %s: cannot start %s: %s", s->name, s->bin, why);
            free(why);
        }
    }
    double spawn_ms = (double)(mesh_native_now_ns() - ts) / 1e6;
    int child_ends[] = {inp[0], outp[1], errp[1], stp[1]};
    close_all(child_ends, 4);
    for (size_t i = 0; i < no; i++) free(owned[i]);
    for (size_t i = 0; i < k; i++) free(env[i]);
    free(env);
    if (!ok) {
        int parent_ends[] = {inp[1], outp[0], errp[0], stp[0]};
        close_all(parent_ends, 4);
        return false;
    }
    proc *p = mesh_alloc(sizeof *p);
    atomic_init(&p->refs, 1);
    p->pid = pid;
    p->name = mesh_strdup(s->name);
    p->stdin_fd = inp[1];
    pthread_mutex_init(&p->stdin_mu, NULL);
    pthread_mutex_init(&p->tail_mu, NULL);
    pthread_mutex_init(&p->status_mu, NULL);
    pthread_mutex_init(&p->sample_mu, NULL);
    p->sample_t = mesh_native_now_ns();
    p->limits = mesh_native_limits_bits(l);
    reader *sr = mesh_alloc(sizeof *sr);
    proc_ref(p);
    *sr = (reader){p, stp[0], NULL, NULL};
    if (!start_thread(status_reader, sr)) mesh_worker_fatal_thread();
    mkdir(h->logs, 0755);
    char *lp = mesh_fmt("%s/%s.log", h->logs, s->name);
    logfile *lf = mesh_alloc(sizeof *lf);
    atomic_init(&lf->refs, 2);
    lf->f = fopen(lp, "a");
    pthread_mutex_init(&lf->mu, NULL);
    free(lp);
    int outs[] = {outp[0], errp[0]};
    for (int i = 0; i < 2; i++) {
        out_reader *r = mesh_alloc(sizeof *r);
        proc_ref(p);
        *r = (out_reader){p, outs[i], lf};
        if (!start_thread(output_reader, r)) mesh_worker_fatal_thread();
    }
    pthread_mutex_lock(&h->mu);
    h->procs = mesh_realloc(h->procs, (h->nprocs + 1) * sizeof *h->procs);
    proc_ref(p);
    h->procs[h->nprocs++] = p;
    pthread_mutex_unlock(&h->mu);
    atomic_fetch_add(&h->spawns, 1);
    atomic_fetch_add(&mesh_cpuuse_worker_spawns, 1);
    atomic_fetch_add(&s->spawns, 1);
    /* Up to `ready` (its setup done), or its death, or READY. */
    bool good = false;
    for (;;) {
        uint32_t abi = atomic_load(&p->ready);
        int st;
        if (abi != 0) {
            if (abi != MESH_NATIVE_ABI)
                mesh_errf(err, "abi: %s speaks native worker ABI %u, meshd %u (rebuild it)", s->name, (unsigned)abi, (unsigned)MESH_NATIVE_ABI);
            else good = true;
            break;
        }
        uint64_t el = mesh_native_now_ns() - t0;
        if (proc_exited(p, &st)) {
            /* Its last output may still be in the pipes: give the readers a moment. */
            for (int i = 0; i < 100 && atomic_load(&p->outs_done) < 2; i++) mesh_native_sleep_us(1000);
            char *d = mesh_describe_status(st), *tail = proc_tail_text(p);
            mesh_errf(err, "load: %s worker %d exited before it was ready (%s); output: %s", s->name, (int)pid, d, tail);
            free(d);
            free(tail);
            break;
        }
        if (atomic_load(&p->eof) && el > 200000000u && !proc_exited(p, NULL)) {
            mesh_errf(err, "load: %s worker %d closed its status pipe without `ready` (not a mesh-worker program?)", s->name, (int)pid);
            break;
        }
        if (el > (uint64_t)MESH_NATIVE_READY_MS * 1000000u) {
            char *tail = proc_tail_text(p);
            mesh_errf(err, "load: %s worker %d not ready in %us; output: %s", s->name, (int)pid, MESH_NATIVE_READY_MS / 1000, tail);
            free(tail);
            break;
        }
        mesh_native_sleep_us(200);
    }
#if defined(__linux__)
    if (good && mesh_native_limits_scoped(l)) good = mesh_native_check_cgroup(pid, l, err);
#endif
    if (good && s->gpu && h->cfg.gpu == MESH_GPU_ACCESS_MPS && h->cfg.check_gpu_client) {
        char *why = NULL;
        good = h->cfg.check_gpu_client(pid, &why);
        if (!good) mesh_errf(err, "load: %s: device gpu: %s", s->name, why);
        free(why);
    }
    if (!good) {
        proc_kill(p);
        host_forget(h, p);
        proc_unref(p);
        return false;
    }
    mesh_native_limits none = {0, 0, 0, false};
    char *lim = mesh_native_limits_eq(l, &none) ? mesh_strdup("") : NULL;
    if (!lim) {
        char *d = mesh_native_limits_debug(l);
        lim = mesh_fmt("; limits %s", d);
        free(d);
    }
    mesh_info("native: %s worker %d ready in %.1f ms (spawn %.2f ms; %s%s)", s->name, (int)pid, (double)(mesh_native_now_ns() - t0) / 1e6,
              spawn_ms, s->bin, lim);
    free(lim);
    *out = p;
    return true;
}

void mesh_native_cleanup(void) {
    pthread_mutex_lock(&hosts_mu);
    proc **all = NULL;
    size_t n = 0;
    for (size_t i = 0; i < nhosts; i++) {
        mesh_native_host *h = hosts[i];
        pthread_mutex_lock(&h->mu);
        all = mesh_realloc(all, (n + h->nprocs + 1) * sizeof *all);
        for (size_t j = 0; j < h->nprocs; j++) all[n++] = h->procs[j];
        h->nprocs = 0;
        pthread_mutex_unlock(&h->mu);
    }
    pthread_mutex_unlock(&hosts_mu);
    for (size_t i = 0; i < n; i++) proc_tell_exit(all[i]);
    uint64_t t0 = mesh_native_now_ns();
    for (;;) {
        bool any = false;
        for (size_t i = 0; i < n; i++) any = any || !proc_exited(all[i], NULL);
        if (!any || mesh_native_now_ns() - t0 > 2000000000u) break;
        mesh_native_sleep_us(2000);
    }
    for (size_t i = 0; i < n; i++)
        if (!proc_exited(all[i], NULL)) {
            mesh_error("native: %s worker %d still there at exit; killing it", all[i]->name, (int)all[i]->pid);
            proc_kill(all[i]);
        }
    if (n) mesh_info("native: %zu worker process(es) gone", n);
    for (size_t i = 0; i < n; i++) proc_unref(all[i]);
    free(all);
}
