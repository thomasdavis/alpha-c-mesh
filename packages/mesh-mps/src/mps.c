/* mps.rs's workers (Mps, Proc, Worker, the pool), the dual-mode GPU and the CUDA backend. */
#include "mps_internal.h"

#include "mesh/loader.h"

#include "mesh/abi/base.h"
#include "mesh/abi/backend.h"
#include "mesh/abi/host_status.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- globals: the daemon, live workers, the server, native GPU clients, the pool ---------- */

typedef struct mps mps;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static char *daemon_dir;
static bool daemon_temporary;
static pid_t *workers;
static size_t nworkers;
static mps *server;
static pid_t *native_clients;
static size_t nnative;

void mps_set_daemon(const char *dir, bool temporary) {
    pthread_mutex_lock(&g_mu);
    free(daemon_dir);
    daemon_dir = mesh_strdup(dir);
    daemon_temporary = temporary;
    pthread_mutex_unlock(&g_mu);
}

static void pids_add(pid_t **v, size_t *n, pid_t p) {
    *v = mesh_realloc(*v, (*n + 1) * sizeof **v);
    (*v)[(*n)++] = p;
}

static void pids_remove(pid_t *v, size_t *n, pid_t p) {
    for (size_t i = 0; i < *n; i++)
        if (v[i] == p) {
            v[i] = v[--*n];
            return;
        }
}

static bool pids_has(const pid_t *v, size_t n, pid_t p) {
    for (size_t i = 0; i < n; i++)
        if (v[i] == p) return true;
    return false;
}

static char *pids_debug(const pid_t *v, size_t n) {
    mesh_text t = {0};
    mesh_text_putc(&t, '[');
    for (size_t i = 0; i < n; i++) mesh_text_printf(&t, "%s%d", i ? ", " : "", (int)v[i]);
    mesh_text_putc(&t, ']');
    return mesh_text_take(&t);
}

/* ---- worker processes -------------------------------------------------------------------- */

typedef struct reply {
    uint8_t op;
    uint8_t *body;
    size_t n;
    struct reply *next;
} reply;

/* A worker process.  Refcounted: its owner and its reader thread. */
typedef struct proc {
    atomic_int refs;
    pid_t pid;
    int in_fd, out_fd;
    pthread_mutex_t in_mu;  /* one request at a time */
    pthread_mutex_t ask_mu; /* the replies */
    pthread_mutex_t rq_mu;
    pthread_cond_t rq_cv;
    reply *rq_head, *rq_tail;
    bool rq_closed;
    /* The last launch's generation, and the last one whose stream finished. */
    _Atomic uint32_t gen, done;
    atomic_bool dead;
    pthread_mutex_t err_mu;
    char *err;
    pthread_mutex_t reap_mu;
    bool reaped;
} proc;

static void proc_unref(proc *p) {
    if (!p || atomic_fetch_sub(&p->refs, 1) != 1) return;
    for (reply *r = p->rq_head; r;) {
        reply *n = r->next;
        free(r->body);
        free(r);
        r = n;
    }
    if (p->in_fd >= 0) close(p->in_fd);
    free(p->err);
    free(p);
}

static bool proc_reaped(proc *p) {
    pthread_mutex_lock(&p->reap_mu);
    if (!p->reaped) {
        int st;
        if (waitpid(p->pid, &st, WNOHANG) == p->pid) p->reaped = true;
    }
    bool r = p->reaped;
    pthread_mutex_unlock(&p->reap_mu);
    return r;
}

static void *proc_reader(void *a) {
    proc *p = a;
    uint8_t op, *b;
    size_t n;
    while (mesh_mps_read_frame(p->out_fd, &op, &b, &n)) {
        if (op == MESH_W_DONE) {
            if (mesh_mps_u32_at(b, n, 4) == 0) {
                pthread_mutex_lock(&p->err_mu);
                free(p->err);
                p->err = mesh_fmt("worker %d: %.*s", (int)p->pid, (int)(n > 8 ? n - 8 : 0), n > 8 ? (const char *)b + 8 : "");
                pthread_mutex_unlock(&p->err_mu);
            }
            atomic_store(&p->done, mesh_mps_u32_at(b, n, 0));
            free(b);
            continue;
        }
        reply *r = mesh_alloc(sizeof *r);
        *r = (reply){op, b, n, NULL};
        pthread_mutex_lock(&p->rq_mu);
        if (p->rq_tail) p->rq_tail->next = r;
        else p->rq_head = r;
        p->rq_tail = r;
        pthread_cond_signal(&p->rq_cv);
        pthread_mutex_unlock(&p->rq_mu);
    }
    close(p->out_fd);
    atomic_store(&p->dead, true);
    pthread_mutex_lock(&p->rq_mu);
    p->rq_closed = true;
    pthread_cond_broadcast(&p->rq_cv);
    pthread_mutex_unlock(&p->rq_mu);
    proc_unref(p);
    return NULL;
}

static bool proc_send(proc *p, uint8_t op, const uint8_t *body, size_t n, char **err) {
    pthread_mutex_lock(&p->in_mu);
    bool ok = p->in_fd >= 0 && mesh_mps_write_frame(p->in_fd, op, body, n);
    int e = errno;
    pthread_mutex_unlock(&p->in_mu);
    if (!ok) {
        char *s = mps_os_error(e ? e : EPIPE);
        mesh_errf(err, "worker %d: %s", (int)p->pid, s);
        free(s);
    }
    return ok;
}

/* Rust's Duration Debug for whole seconds: "30s", "2s". */
static bool proc_ask(proc *p, uint8_t op, const uint8_t *body, size_t n, uint8_t want, unsigned within_ms, uint8_t **out, size_t *on,
                     char **err) {
    pthread_mutex_lock(&p->ask_mu);
    if (!proc_send(p, op, body, n, err)) {
        pthread_mutex_unlock(&p->ask_mu);
        return false;
    }
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += within_ms / 1000;
    dl.tv_nsec += (long)(within_ms % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) {
        dl.tv_sec++;
        dl.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&p->rq_mu);
    int rc = 0;
    while (!p->rq_head && !p->rq_closed && rc == 0) rc = pthread_cond_timedwait(&p->rq_cv, &p->rq_mu, &dl);
    reply *r = p->rq_head;
    if (r) {
        p->rq_head = r->next;
        if (!p->rq_head) p->rq_tail = NULL;
    }
    bool closed = p->rq_closed;
    pthread_mutex_unlock(&p->rq_mu);
    pthread_mutex_unlock(&p->ask_mu);
    bool ok = false;
    if (r) {
        if (r->op == want && mesh_mps_u32_at(r->body, r->n, 0) == 1) {
            size_t k = r->n > 4 ? r->n - 4 : 0;
            *out = mesh_alloc(k ? k : 1);
            if (k) memcpy(*out, r->body + 4, k);
            *on = k;
            ok = true;
        } else if (r->op == want) {
            mesh_errf(err, "%.*s", (int)(r->n > 4 ? r->n - 4 : 0), r->n > 4 ? (const char *)r->body + 4 : "");
        } else {
            mesh_errf(err, "worker %d: reply %u, expected %u", (int)p->pid, r->op, want);
        }
        free(r->body);
        free(r);
    } else if (closed) {
        mesh_errf(err, "worker %d exited", (int)p->pid);
    } else {
        mesh_errf(err, "worker %d: no reply in %us", (int)p->pid, within_ms / 1000);
    }
    return ok;
}

/* *done = the last launch's stream finished; false with *err on a stream error or a death. */
static bool proc_query(proc *p, bool *done, char **err) {
    pthread_mutex_lock(&p->err_mu);
    char *e = p->err ? mesh_strdup(p->err) : NULL;
    pthread_mutex_unlock(&p->err_mu);
    if (e) {
        mesh_errf(err, "%s", e);
        free(e);
        return false;
    }
    bool d = atomic_load(&p->done) == atomic_load(&p->gen);
    if (!d && atomic_load(&p->dead)) return mesh_errf(err, "worker %d exited while its kernel ran", (int)p->pid);
    *done = d;
    return true;
}

/* Alive, no stream error, nothing running: fit for another image. */
static bool proc_idle(proc *p) {
    bool done = false;
    return !atomic_load(&p->dead) && proc_query(p, &done, NULL) && done;
}

static void *retire_thread(void *a) {
    proc *p = a;
    uint64_t t0 = mps_now_ns();
    while (mps_now_ns() - t0 < 2000000000u) {
        if (proc_reaped(p)) {
            mesh_info("mps: worker %d exited", (int)p->pid);
            proc_unref(p);
            return NULL;
        }
        mps_sleep_us(1000);
    }
    mesh_error("mps: worker %d did not exit in 2 s; killing it", (int)p->pid);
    kill(p->pid, SIGKILL);
    uint64_t t1 = mps_now_ns();
    while (!proc_reaped(p)) {
        if (mps_now_ns() - t1 > 2000000000u) {
            mesh_error("mps: worker %d not reaped 2 s after SIGKILL; left behind", (int)p->pid);
            break;
        }
        mps_sleep_us(1000);
    }
    proc_unref(p);
    return NULL;
}

/* W_QUIT, then reaped off the driver thread (killed if it lingers).  Takes the reference. */
static void proc_retire(proc *p) {
    proc_send(p, MESH_W_QUIT, NULL, 0, NULL);
    pthread_mutex_lock(&g_mu);
    pids_remove(workers, &nworkers, p->pid);
    pthread_mutex_unlock(&g_mu);
    pthread_t t;
    if (pthread_create(&t, NULL, retire_thread, p) == 0) pthread_detach(t);
    else retire_thread(p);
}

/* Idle workers, kept with their context (module unloaded) for the next load. */
static pthread_mutex_t pool_mu = PTHREAD_MUTEX_INITIALIZER;
static proc **pool;
static size_t npool;
static atomic_size_t pool_cap;

/* ---- Mps ------------------------------------------------------------------------------------ */

struct mps {
    char *dir;
    char *mailbox; /* the path workers open the mailbox's memfd by */
    uint32_t sms;  /* 0: no green context */
    char *exe;
    pthread_mutex_t server_mu;
    char *server; /* the server's pid once a worker was seen in its client list */
};

typedef struct {
    uint32_t rt_abi, regs, per_sm, local, sms;
    uint64_t scratch;
} winfo;

/* One image in its own MPS client process. */
typedef struct {
    proc *p; /* until dropped */
    char *name;
    winfo info;
} worker;

static bool mps_check_clients(mps *m, const pid_t *want, size_t nwant, char **err) {
    char *servers = mps_control(m->dir, "get_server_list", err);
    if (!servers) return false;
    char *nl = strchr(servers, '\n');
    if (nl) *nl = 0;
    const char *sv = servers;
    size_t sn = strlen(sv);
    mesh_trim(&sv, &sn);
    if (sn == 0) {
        char *w = pids_debug(want, nwant);
        mesh_errf(err, "mps: workers %s are up but our daemon runs no server: they are not MPS clients; refusing", w);
        free(w);
        free(servers);
        return false;
    }
    char *server_pid = mesh_strndup(sv, sn);
    free(servers);
    char *req = mesh_fmt("get_client_list %s", server_pid);
    char *clients = mps_control(m->dir, req, err);
    free(req);
    if (!clients) {
        free(server_pid);
        return false;
    }
    pid_t *pids = NULL;
    size_t np = 0;
    for (char *line = clients; line && *line;) {
        char *next = strchr(line, '\n');
        if (next) *next++ = 0;
        const char *s = line;
        size_t n = strlen(s);
        mesh_trim(&s, &n);
        uint32_t v;
        if (mesh_parse_u32(s, n, 10, &v)) pids_add(&pids, &np, (pid_t)v);
        line = next;
    }
    free(clients);
    bool ok = true;
    for (size_t i = 0; i < nwant && ok; i++)
        if (!pids_has(pids, np, want[i])) {
            char *d = pids_debug(pids, np);
            ok = mesh_errf(err, "mps: worker %d is not among server %s's clients %s; refusing", (int)want[i], server_pid, d);
            free(d);
        }
    if (ok) {
        pthread_mutex_lock(&g_mu);
        pid_t *foreign = NULL;
        size_t nf = 0;
        for (size_t i = 0; i < np; i++)
            if (!pids_has(workers, nworkers, pids[i]) && !pids_has(native_clients, nnative, pids[i])) pids_add(&foreign, &nf, pids[i]);
        pthread_mutex_unlock(&g_mu);
        if (nf) {
            char *d = pids_debug(foreign, nf);
            mesh_error("mps: server %s has clients that are not meshd's workers: %s", server_pid, d);
            free(d);
        }
        free(foreign);
        pthread_mutex_lock(&m->server_mu);
        if (!m->server || strcmp(m->server, server_pid) != 0) {
            char *d = pids_debug(pids, np);
            mesh_info("mps: server pid %s, clients %s", server_pid, d);
            free(d);
            free(m->server);
            m->server = mesh_strdup(server_pid);
        }
        pthread_mutex_unlock(&m->server_mu);
    }
    free(pids);
    free(server_pid);
    return ok;
}

static bool mps_spawn(mps *m, proc **out, char **err) {
    int in[2], out_p[2];
    if (!mesh_pipe_cloexec(in, err) || !mesh_pipe_cloexec(out_p, err)) return false;
    char *sms = m->sms ? mesh_fmt("%u", (unsigned)m->sms) : NULL;
    char *argv[] = {m->exe, "worker", "--mailbox", m->mailbox, sms ? "--sms" : NULL, sms, NULL};
    char *env[] = {mesh_fmt("%s=%s/pipe", MESH_MPS_PIPE_ENV, m->dir), mesh_fmt("%s=%s/log", MESH_MPS_LOG_ENV, m->dir), NULL};
    mesh_spawn_fd fds[] = {{in[0], 0}, {out_p[1], 1}, {2, 2}};
    pid_t pid;
    char *why = NULL;
    /* No PR_SET_PDEATHSIG: it fires when the spawning (JIT) thread exits.  A
     * worker exits at EOF on stdin instead, which meshd's death also gives. */
    bool ok = mesh_spawn(argv, env, fds, 3, &pid, &why);
    free(sms);
    free(env[0]);
    free(env[1]);
    close(in[0]);
    close(out_p[1]);
    if (!ok) {
        const char *e = strstr(why, ": ");
        mesh_errf(err, "load: mps worker: %s", e ? e + 2 : why);
        free(why);
        close(in[1]);
        close(out_p[0]);
        return false;
    }
    proc *p = mesh_alloc(sizeof *p);
    atomic_init(&p->refs, 2);
    p->pid = pid;
    p->in_fd = in[1];
    p->out_fd = out_p[0];
    pthread_mutex_init(&p->in_mu, NULL);
    pthread_mutex_init(&p->ask_mu, NULL);
    pthread_mutex_init(&p->rq_mu, NULL);
    pthread_cond_init(&p->rq_cv, NULL);
    pthread_mutex_init(&p->err_mu, NULL);
    pthread_mutex_init(&p->reap_mu, NULL);
    pthread_mutex_lock(&g_mu);
    pids_add(&workers, &nworkers, pid);
    pthread_mutex_unlock(&g_mu);
    pthread_t t;
    if (pthread_create(&t, NULL, proc_reader, p) != 0) abort();
    pthread_detach(t);
    *out = p;
    return true;
}

static void worker_drop(worker *w);

static bool mps_preload(mps *m, const char *name, const uint8_t *cubin, size_t n, uint32_t threads, uint32_t smem, uint32_t scratch,
                        worker **out, char **err) {
    uint64_t t0 = mps_now_ns();
    proc *p = NULL;
    for (;;) {
        pthread_mutex_lock(&pool_mu);
        proc *q = npool ? pool[--npool] : NULL;
        pthread_mutex_unlock(&pool_mu);
        if (!q) break;
        if (proc_idle(q)) {
            p = q;
            break;
        }
        proc_retire(q);
    }
    bool fresh = p == NULL;
    if (!p && !mps_spawn(m, &p, err)) return false;
    pid_t pid = p->pid;
    worker *w = mesh_alloc(sizeof *w);
    w->p = p;
    w->name = mesh_strdup(name);
    uint8_t *body = NULL;
    size_t bn = 0;
    mesh_mps_put_u32(&body, &bn, threads);
    mesh_mps_put_u32(&body, &bn, smem);
    mesh_mps_put_u32(&body, &bn, scratch);
    mesh_mps_put_u32(&body, &bn, (uint32_t)strlen(name));
    mesh_mps_put_bytes(&body, &bn, name, strlen(name));
    mesh_mps_put_bytes(&body, &bn, cubin, n);
    uint8_t *b = NULL;
    size_t k = 0;
    char *why = NULL;
    bool ok = proc_ask(p, MESH_W_LOAD, body, bn, MESH_W_LOADED, MESH_MPS_WORKER_LOAD_MS, &b, &k, &why);
    free(body);
    if (!ok) {
        mesh_errf(err, "load: %s", why);
        free(why);
        worker_drop(w);
        return false;
    }
    w->info = (winfo){mesh_mps_u32_at(b, k, 0), mesh_mps_u32_at(b, k, 4), mesh_mps_u32_at(b, k, 8), mesh_mps_u32_at(b, k, 12),
                      mesh_mps_u32_at(b, k, 16), mesh_mps_u64_at(b, k, 28)};
    uint64_t dev = mesh_mps_u64_at(b, k, 20);
    free(b);
    if (m->sms && m->sms != w->info.sms) {
        mesh_errf(err, "load: the worker's green context has %u SMs, not %u", (unsigned)w->info.sms, (unsigned)m->sms);
        worker_drop(w);
        return false;
    }
    if (fresh && !mps_check_clients(m, &pid, 1, err)) {
        worker_drop(w);
        return false;
    }
    mesh_info("mps: worker %d (%s) for %s: module loaded, mailbox at device 0x%llx, %u SMs, in %.1f ms", (int)pid, fresh ? "new" : "pooled",
              name, (unsigned long long)dev, (unsigned)w->info.sms, (double)(mps_now_ns() - t0) / 1e6);
    *out = w;
    return true;
}

static bool mps_prewarm(mps *m, uint32_t n, char **err) {
    atomic_store(&pool_cap, n);
    if (n == 0) return true;
    uint64_t t0 = mps_now_ns();
    proc **procs = mesh_alloc(n * sizeof *procs);
    pid_t *pids = mesh_alloc(n * sizeof *pids);
    uint32_t made = 0;
    bool ok = true;
    for (; made < n && ok; made++) {
        ok = mps_spawn(m, &procs[made], err);
        if (!ok) break;
        pids[made] = procs[made]->pid;
    }
    for (uint32_t i = 0; i < made && ok; i++) {
        uint8_t *b = NULL;
        size_t k;
        char *why = NULL;
        ok = proc_ask(procs[i], MESH_W_READY, NULL, 0, MESH_W_READY, MESH_MPS_WORKER_LOAD_MS, &b, &k, &why);
        free(b);
        if (!ok) {
            mesh_errf(err, "mps: pool: %s", why);
            free(why);
        }
    }
    if (ok) ok = mps_check_clients(m, pids, made, err);
    if (ok) {
        char *d = pids_debug(pids, made);
        mesh_info("mps: pool of %u idle workers %s ready in %.0f ms", (unsigned)n, d, (double)(mps_now_ns() - t0) / 1e6);
        free(d);
        pthread_mutex_lock(&pool_mu);
        pool = mesh_realloc(pool, (npool + made) * sizeof *pool);
        for (uint32_t i = 0; i < made; i++) pool[npool++] = procs[i];
        pthread_mutex_unlock(&pool_mu);
    } else {
        for (uint32_t i = 0; i < made; i++) proc_retire(procs[i]);
    }
    free(procs);
    free(pids);
    return ok;
}

static bool worker_launch(worker *w, uint32_t slot, uint32_t nodes, char **err) {
    proc *p = w->p;
    uint32_t gen = atomic_load(&p->gen) + 1;
    atomic_store(&p->gen, gen);
    uint8_t *body = NULL;
    size_t bn = 0;
    mesh_mps_put_u32(&body, &bn, gen);
    mesh_mps_put_u32(&body, &bn, slot);
    mesh_mps_put_u32(&body, &bn, nodes);
    uint8_t *b = NULL;
    size_t k;
    char *why = NULL;
    bool ok = proc_ask(p, MESH_W_LAUNCH, body, bn, MESH_W_LAUNCHED, MESH_MPS_WORKER_LAUNCH_MS, &b, &k, &why);
    free(body);
    free(b);
    if (!ok) {
        mesh_errf(err, "%s (%s)", w->name, why);
        free(why);
    }
    return ok;
}

static bool worker_query(worker *w, bool *done, char **err) {
    char *why = NULL;
    if (proc_query(w->p, done, &why)) return true;
    mesh_errf(err, "%s: %s", w->name, why);
    free(why);
    return false;
}

/* Back to the pool with its module unloaded (W_UNLOAD), unless it is unfit or
 * the pool is full: then the process exits. */
static void worker_drop(worker *w) {
    proc *p = w->p;
    if (p) {
        pthread_mutex_lock(&pool_mu);
        bool keep = proc_idle(p) && npool < atomic_load(&pool_cap) && proc_send(p, MESH_W_UNLOAD, NULL, 0, NULL);
        if (keep) {
            pool = mesh_realloc(pool, (npool + 1) * sizeof *pool);
            pool[npool++] = p;
        }
        pthread_mutex_unlock(&pool_mu);
        if (!keep) proc_retire(p);
    }
    free(w->name);
    free(w);
}

/* ---- globals' accessors ------------------------------------------------------------------- */

char *mesh_mps_native_pipe_dir(void) {
    pthread_mutex_lock(&g_mu);
    char *d = server ? mesh_fmt("%s/pipe", server->dir) : NULL;
    pthread_mutex_unlock(&g_mu);
    return d;
}

bool mesh_mps_check_native(pid_t pid, char **err) {
    pthread_mutex_lock(&g_mu);
    mps *m = server;
    if (m) pids_add(&native_clients, &nnative, pid);
    pthread_mutex_unlock(&g_mu);
    if (!m) return mesh_errf(err, "mps: no server (blink mode)");
    bool ok = mps_check_clients(m, &pid, 1, err);
    if (!ok) {
        pthread_mutex_lock(&g_mu);
        pids_remove(native_clients, &nnative, pid);
        pthread_mutex_unlock(&g_mu);
    }
    return ok;
}

void mesh_mps_cleanup(void) {
    /* Idle ones see EOF and exit; then every worker is killed and reaped. */
    pthread_mutex_lock(&pool_mu);
    proc **idle = pool;
    size_t nidle = npool;
    pool = NULL;
    npool = 0;
    pthread_mutex_unlock(&pool_mu);
    for (size_t i = 0; i < nidle; i++) {
        pthread_mutex_lock(&idle[i]->in_mu);
        if (idle[i]->in_fd >= 0) close(idle[i]->in_fd);
        idle[i]->in_fd = -1;
        pthread_mutex_unlock(&idle[i]->in_mu);
    }
    pthread_mutex_lock(&g_mu);
    pid_t *kids = workers;
    size_t nk = nworkers;
    workers = NULL;
    nworkers = 0;
    char *dir = daemon_dir;
    bool temporary = daemon_temporary;
    daemon_dir = NULL;
    pthread_mutex_unlock(&g_mu);
    for (size_t i = 0; i < nk; i++) kill(kids[i], SIGKILL);
    /* Reaped within a shared deadline: one stuck in the kernel is left behind. */
    uint64_t t0 = mps_now_ns();
    size_t left = nk;
    while (left && mps_now_ns() - t0 < 500000000u) {
        left = 0;
        for (size_t i = 0; i < nk; i++) {
            if (kids[i] == 0) continue;
            int st;
            pid_t r = waitpid(kids[i], &st, WNOHANG);
            if (r == kids[i] || (r < 0 && errno == ECHILD)) kids[i] = 0;
            else left++;
        }
        if (left) mps_sleep_us(1000);
    }
    for (size_t i = 0; i < nk; i++)
        if (kids[i]) mesh_error("mps: worker %d not reaped 500 ms after SIGKILL; left behind", (int)kids[i]);
    free(kids);
    for (size_t i = 0; i < nidle; i++) proc_unref(idle[i]);
    free(idle);
    if (!dir) return;
    mps_quit_daemon(dir, temporary);
    free(dir);
}

/* ---- the GPU in either mode, and the backend -------------------------------------------- */

typedef enum { K_ONE, K_WORKER } kind;

typedef struct {
    kind kind;
    mesh_cuda_kernel one;
    worker *w;
    struct cuda_backend *b;
} ckernel;

typedef struct cuda_backend {
    bool mps_mode;
    mesh_cuda_gpu gpu; /* blink: the context */
    uint64_t dev;      /* blink: the mailbox's device address */
    mps *m;            /* mps: the server */
    bool host_atomics;
    mesh_cuda_limits limits;
    mesh_shm shm;
    mesh_caps caps;
    mesh_nvjitlink jit;
    char *arch;
    mesh_compiler comp;
} cuda_backend;

static cuda_backend *cb(const mesh_backend *b) { return b->self; }

static bool k_regs(const mesh_kernel *k, uint32_t *out, char **err) {
    ckernel *c = k->self;
    if (c->kind == K_WORKER) {
        *out = c->w->info.regs;
        return true;
    }
    return mesh_cuda_kernel_regs(&c->one, out, err);
}
static bool k_local(const mesh_kernel *k, uint32_t *out, char **err) {
    ckernel *c = k->self;
    if (c->kind == K_WORKER) {
        *out = c->w->info.local;
        return true;
    }
    return mesh_cuda_kernel_local_bytes(&c->one, out, err);
}
static bool k_per_sm(const mesh_kernel *k, uint32_t *out, char **err) {
    ckernel *c = k->self;
    if (c->kind == K_WORKER) {
        *out = c->w->info.per_sm;
        return true;
    }
    return mesh_cuda_kernel_max_blocks_per_sm(&c->one, out, err);
}
static uint64_t k_scratch(const mesh_kernel *k) {
    ckernel *c = k->self;
    return c->kind == K_WORKER ? c->w->info.scratch : c->one.scratch;
}
static bool k_global(const mesh_kernel *k, const char *name, uint32_t *out, char **err) {
    ckernel *c = k->self;
    if (c->kind == K_ONE) return mesh_cuda_kernel_global_u32(&c->one, name, out, err);
    if (strcmp(name, MESH_RT_ABI_SYMBOL) == 0) {
        *out = c->w->info.rt_abi;
        return true;
    }
    return mesh_errf(err, "global %s: a worker reports only %s", name, MESH_RT_ABI_SYMBOL);
}
static void k_drop(mesh_kernel *k) {
    ckernel *c = k->self;
    /* A context module dropped without its unload stays loaded (as the Rust's). */
    if (c->kind == K_WORKER) worker_drop(c->w);
    free(c);
}

static const mesh_kernel_ops kernel_ops = {MESH_BACKEND_CUDA, k_regs, k_local, k_per_sm, k_scratch, k_global, k_drop};

static mesh_kernel *new_kernel(ckernel *c) {
    mesh_kernel *k = mesh_alloc(sizeof *k);
    *k = (mesh_kernel){&kernel_ops, c};
    return k;
}

/* PTX -> cubin with nvJitLink for this GPU's arch (and, mps, the worker preload). */
static bool c_needs_ptx(const mesh_compiler *c) {
    (void)c;
    return true;
}

static bool c_prepare(const mesh_compiler *c, mesh_payload *src, const uint64_t *expect, const char *root, mesh_prepared *out, char **err) {
    (void)root;
    cuda_backend *b = c->self;
    *out = (mesh_prepared){0};
    mesh_manifest m;
    if (!mesh_check_manifest(src, MESH_BACKEND_CUDA, &m, err)) {
        mesh_payload_free(src);
        return false;
    }
    uint64_t hash = mesh_image_hash(src->ptx, src->ptx_len, m.param);
    uint32_t reg_cap = mesh_cuda_reg_cap(&b->limits, m.warps);
    uint8_t *cubin = NULL;
    size_t cn = 0;
    if (!mesh_check_hash(m.name, "PTX", hash, expect, err) ||
        !mesh_cuda_limits_check(&b->limits, m.name, m.warps, mesh_manifest_smem(&m), err))
        goto fail;
    uint64_t t0 = mps_now_ns();
    char *why = NULL;
    if (!mesh_nvjitlink_ptx_to_cubin(&b->jit, src->ptx, src->ptx_len, b->arch, reg_cap, &cubin, &cn, &why)) {
        for (char *s = why; *s; s++)
            if (*s == '\n') {
                /* replace '\n' with " | " */
                size_t at = (size_t)(s - why);
                char *r = mesh_fmt("%.*s | %s", (int)at, why, s + 1);
                free(why);
                why = r;
                s = why + at + 2;
            }
        mesh_errf(err, "jit: %s", why);
        free(why);
        goto fail;
    }
    double compile_ms = (double)(mps_now_ns() - t0) / 1e6;
    mesh_kernel *k = NULL;
    if (b->mps_mode) {
        worker *w;
        if (!mps_preload(b->m, m.name, cubin, cn, mesh_manifest_threads(&m), mesh_manifest_smem(&m), m.scratch_bytes, &w, err)) goto fail;
        ckernel *ck = mesh_alloc(sizeof *ck);
        *ck = (ckernel){K_WORKER, {0}, w, b};
        k = new_kernel(ck);
    }
    out->manifest = m;
    out->hash = hash;
    out->code = cubin;
    out->code_len = cn;
    out->compile_ms = compile_ms;
    out->src = *src;
    *src = (mesh_payload){0};
    out->kernel = k;
    out->exec = MESH_BACKEND_CUDA;
    out->reg_cap = reg_cap;
    return true;
fail:
    free(cubin);
    mesh_manifest_free(&m);
    mesh_payload_free(src);
    return false;
}

static const mesh_compiler_ops compiler_ops = {c_needs_ptx, c_prepare};

static const mesh_caps *b_caps(const mesh_backend *b) { return &cb(b)->caps; }
static bool b_isolated(const mesh_backend *b) { return cb(b)->mps_mode; }
static bool b_resident(const mesh_backend *b) {
    (void)b;
    return true;
}
static uint32_t b_isolation(const mesh_backend *b) { return cb(b)->mps_mode ? MESH_ISOLATION_MPS : MESH_ISOLATION_BLINK; }
static bool b_mailbox(const mesh_backend *b, uint8_t **host, uint64_t *dev, char **err) {
    (void)err;
    *host = cb(b)->shm.base;
    *dev = cb(b)->mps_mode ? 0 : cb(b)->dev;
    return true;
}
static bool b_make_current(const mesh_backend *b, char **err) { return cb(b)->mps_mode ? true : mesh_cuda_make_current(&cb(b)->gpu, err); }
static const mesh_compiler *b_compiler(const mesh_backend *b) { return &cb(b)->comp; }

static bool b_load(const mesh_backend *b, mesh_prepared *p, mesh_kernel **out, char **err) {
    cuda_backend *c = cb(b);
    mesh_kernel *pre = p->kernel;
    p->kernel = NULL;
    if (pre && pre->ops != &kernel_ops) {
        mesh_kernel_drop(pre);
        return mesh_errf(err, "internal: a non-CUDA image on the CUDA backend");
    }
    if (!c->mps_mode && !pre) {
        ckernel *k = mesh_alloc(sizeof *k);
        k->kind = K_ONE;
        k->b = c;
        if (!mesh_cuda_load_cubin(&c->gpu, p->code, MESH_PROGRAM_ENTRY, &k->one, err)) {
            free(k);
            return false;
        }
        if (!mesh_cuda_kernel_set_shape(&k->one, mesh_manifest_threads(&p->manifest), mesh_manifest_smem(&p->manifest), err) ||
            !mesh_cuda_kernel_alloc_scratch(&k->one, p->manifest.scratch_bytes, err)) {
            mesh_cuda_unload(&c->gpu, &k->one, NULL);
            free(k);
            return false;
        }
        *out = new_kernel(k);
        return true;
    }
    if (c->mps_mode && pre && ((ckernel *)pre->self)->kind == K_WORKER) {
        *out = pre;
        return true;
    }
    mesh_kernel_drop(pre);
    return mesh_errf(err, "internal: an image prepared for the other isolation mode");
}

static void stream_drop(mesh_stream *s) {
    if (s->self) {
        mesh_cuda_stream_destroy(s->self);
        free(s->self);
    }
}

static bool b_new_stream(const mesh_backend *b, uint32_t slot, const mesh_kernel *k, mesh_stream **out, char **err) {
    (void)k;
    mesh_stream *s = mesh_alloc(sizeof *s);
    *s = (mesh_stream){MESH_BACKEND_CUDA, slot, NULL, stream_drop};
    if (!cb(b)->mps_mode) {
        mesh_cuda_stream *cs = mesh_alloc(sizeof *cs);
        if (!mesh_cuda_new_stream(&cb(b)->gpu, cs, err)) {
            free(cs);
            free(s);
            return false;
        }
        s->self = cs;
    }
    *out = s;
    return true;
}

static bool b_launch(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, uint32_t nodes, uint64_t dev, uint32_t slot, char **err) {
    cuda_backend *c = cb(b);
    ckernel *ck = k->self;
    if (!c->mps_mode && st->self && ck->kind == K_ONE) {
        uint64_t d = dev;
        uint32_t s = slot;
        void *params[] = {&d, &s};
        return mesh_cuda_launch_on(&c->gpu, st->self, &ck->one, nodes, params, err);
    }
    if (c->mps_mode && ck->kind == K_WORKER) return worker_launch(ck->w, slot, nodes, err);
    return mesh_errf(err, "internal: stream/image of the other isolation mode");
}

static bool b_query(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, bool *done, char **err) {
    cuda_backend *c = cb(b);
    ckernel *ck = k->self;
    if (!c->mps_mode && st->self) return mesh_cuda_query(&c->gpu, st->self, done, err);
    if (c->mps_mode && ck->kind == K_WORKER) return worker_query(ck->w, done, err);
    return mesh_errf(err, "internal: stream/image of the other isolation mode");
}

static bool b_unload(const mesh_backend *b, mesh_kernel *k, char **err) {
    cuda_backend *c = cb(b);
    ckernel *ck = k->self;
    bool ok = true;
    if (ck->kind == K_ONE) {
        if (c->mps_mode) ok = mesh_errf(err, "internal: a context module in mps mode");
        else ok = mesh_cuda_unload(&c->gpu, &ck->one, err);
    }
    mesh_kernel_drop(k); /* a worker goes back to the pool */
    return ok;
}

static bool b_stack_limit(const mesh_backend *b, size_t *out, char **err) {
    if (cb(b)->mps_mode) {
        *out = SIZE_MAX;
        return true;
    }
    return mesh_cuda_stack_limit(&cb(b)->gpu, out, err);
}
static bool b_set_stack_limit(const mesh_backend *b, size_t bytes, char **err) {
    return cb(b)->mps_mode ? true : mesh_cuda_set_stack_limit(&cb(b)->gpu, bytes, err);
}
static bool b_module_scope(const mesh_backend *b, bool has_slot, uint32_t slot, uint32_t *scope) {
    if (!cb(b)->mps_mode) return false;
    *scope = has_slot ? slot : MESH_NO_SLOT;
    return true;
}
static void b_destroy(mesh_backend *b) {
    cuda_backend *c = cb(b);
    mesh_caps_free(&c->caps);
    mesh_cuda_gpu_free(&c->gpu);
    mesh_nvjitlink_free(&c->jit);
    free(c->arch);
    free(c);
    free(b);
}

static const mesh_backend_ops backend_ops = {
    b_caps, b_isolated, b_resident, b_isolation, b_mailbox, b_make_current, b_compiler, b_load, b_new_stream, b_launch, b_query,
    b_unload, b_stack_limit, b_set_stack_limit, b_module_scope, NULL, NULL, NULL, NULL, NULL, b_destroy,
};

#define CKD(call, what)                              \
    do {                                             \
        if (!mesh_cu_ck_debug((call), (what), err)) \
            return false;                            \
    } while (0)

/* (name, compute capability, SMs, blocks per SM, host-native atomics, block limits), no context. */
static bool device_info(cuda_backend *c, char **err) {
    if (!mesh_cuda() && !mesh_cuda_load(err)) return false;
    const mesh_cuda_fns *cu = mesh_cuda();
    CKD(cu->cuInit(0), "cuInit");
    if (!mesh_cuda_injected()) {
        char *lib = mesh_cuda_check_lib(err);
        if (!lib) return false;
        free(lib);
    }
    CUdevice dev = 0;
    CKD(cu->cuDeviceGet(&dev, 0), "cuDeviceGet");
    char name[128] = {0};
    CKD(cu->cuDeviceGetName(name, 128, dev), "cuDeviceGetName");
    int a[5];
    const int attrs[5] = {CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR,
                          CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, CU_DEVICE_ATTRIBUTE_MAX_BLOCKS_PER_MULTIPROCESSOR,
                          CU_DEVICE_ATTRIBUTE_HOST_NATIVE_ATOMIC_SUPPORTED};
    for (int i = 0; i < 5; i++) CKD(cu->cuDeviceGetAttribute(&a[i], attrs[i], dev), "cuDeviceGetAttribute");
    if (!mesh_cuda_limits_of(dev, &c->limits, err)) return false;
    c->gpu.name = mesh_strdup(name);
    c->gpu.cc_major = a[0];
    c->gpu.cc_minor = a[1];
    c->gpu.sms_total = (uint32_t)a[2];
    c->gpu.blocks_per_sm = (uint32_t)a[3];
    c->host_atomics = a[4] != 0;
    return true;
}

bool mesh_cuda_backend_open(mesh_isolation_mode iso, uint32_t sms, const char *mps_dir, uint32_t pool_n, const mesh_shm *shm,
                            mesh_nvjitlink *jit, mesh_backend **out, char **err) {
    cuda_backend *c = mesh_alloc(sizeof *c);
    c->shm = *shm;
    c->jit = *jit;
    *jit = (mesh_nvjitlink){0};
    if (iso == MESH_ISOLATION_MODE_BLINK) {
        if (!mesh_cuda_gpu_open(sms, &c->gpu, err) || !mesh_cuda_make_current(&c->gpu, err) ||
            !mesh_cuda_host_register(&c->gpu, shm->base, shm->len, &c->dev, err))
            goto fail;
        c->limits = c->gpu.limits;
        c->host_atomics = c->gpu.host_atomics;
    } else {
        c->mps_mode = true;
        char *dir = mps_dir ? mesh_strdup(mps_dir) : mesh_fmt("/tmp/meshd-mps.%d", (int)getpid());
        if (!mps_start_daemon(dir, mps_dir == NULL, err)) {
            free(dir);
            goto fail;
        }
        /* meshd's own cuInit (device attributes, no context) must join no daemon. */
        char *none = mesh_fmt("%s/none", dir);
        setenv(MESH_MPS_PIPE_ENV, none, 1);
        free(none);
        if (!device_info(c, err)) {
            free(dir);
            goto fail;
        }
        if (sms > c->gpu.sms_total) {
            mesh_errf(err, "--sms %u: the device has %u", (unsigned)sms, (unsigned)c->gpu.sms_total);
            free(dir);
            goto fail;
        }
        c->gpu.sms = sms ? sms : c->gpu.sms_total;
        c->gpu.green = sms != 0;
        mps *m = mesh_alloc(sizeof *m);
        char exe[4096];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (n < 0) {
            char *e = mps_os_error(errno);
            mesh_errf(err, "mps: /proc/self/exe: %s", e);
            free(e);
            free(dir);
            free(m);
            goto fail;
        }
        exe[n] = 0;
        m->dir = dir;
        m->mailbox = mesh_shm_path(shm);
        m->sms = sms;
        m->exe = mesh_strdup(exe);
        pthread_mutex_init(&m->server_mu, NULL);
        mesh_info("mps: mailbox %d bytes in a memfd (%s); workers run %s", MESH_MAILBOX_BYTES, m->mailbox, m->exe);
        c->m = m;
        mesh_backend_on_cleanup(MESH_CLEANUP_MPS, mesh_mps_cleanup);
        if (!mps_prewarm(m, pool_n, err)) goto fail;
        pthread_mutex_lock(&g_mu);
        server = m;
        pthread_mutex_unlock(&g_mu);
    }
    c->arch = mesh_fmt("sm_%d%d", c->gpu.cc_major, c->gpu.cc_minor);
    c->caps = (mesh_caps){MESH_BACKEND_CUDA, mesh_strdup(c->gpu.name), c->gpu.cc_major, c->gpu.cc_minor, c->gpu.sms_total, c->gpu.sms,
                          c->gpu.green, c->gpu.blocks_per_sm, 0, c->limits.max_threads, c->limits.max_shared, mesh_strdup(c->arch)};
    c->comp = (mesh_compiler){&compiler_ops, c};
    mesh_backend *b = mesh_alloc(sizeof *b);
    *b = (mesh_backend){&backend_ops, c};
    *out = b;
    return true;
fail:
    mesh_cuda_gpu_free(&c->gpu);
    mesh_nvjitlink_free(&c->jit);
    free(c);
    return false;
}

CUcontext mesh_cuda_backend_context(const mesh_backend *b) { return cb(b)->mps_mode ? NULL : cb(b)->gpu.ctx; }
bool mesh_cuda_backend_mps(const mesh_backend *b) { return cb(b)->mps_mode; }
bool mesh_cuda_backend_host_atomics(const mesh_backend *b) { return cb(b)->host_atomics; }
