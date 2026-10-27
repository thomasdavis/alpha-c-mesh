/* The driver: its state, the loop, startup and shutdown, statistics, and the commands other threads send. */
#include "driver_int.h"

#include "mesh/cpuuse.h"
#include "mesh/native.h"
#include "mesh/rsfmt.h"
#include "mesh/state_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/* ---------------------------------------------------------------- helpers */

uint32_t drv_gpu_kind(const mesh_driver *d) { return d->gpu->ops->caps(d->gpu)->kind; }

/* The executor of kind exec among the GPU backend and the CPU executors. */
mesh_backend *drv_be(const mesh_driver *d, uint32_t exec) {
    if (exec == drv_gpu_kind(d)) return d->gpu;
    for (size_t i = 0; i < d->ncpus; i++)
        if (d->cpus[i]->ops->caps(d->cpus[i])->kind == exec) return d->cpus[i];
    mesh_error("internal: no executor %s", exec < MESH_BACKENDS_LEN ? MESH_BACKENDS[exec] : "?");
    abort();
}

uint32_t drv_exec_of(const mesh_driver *d, uint32_t s) { return d->slots[s] ? d->slots[s]->image.exec : drv_gpu_kind(d); }

bool drv_in_scope(const mesh_driver *d, bool has_scope, uint32_t scope, uint32_t s) {
    if (has_scope) return scope == s;
    return drv_exec_of(d, s) == drv_gpu_kind(d);
}

const char *drv_name(const dslot *x) { return x->image.manifest.name; }
bool drv_live(const dslot *x) { return x->phase != PH_STOPPED; }

uint32_t drv_state(const dslot *x) {
    if (x->failed && !drv_live(x)) return MESH_ST_FAILED;
    switch (x->phase) {
    case PH_STOPPED: return MESH_ST_LOADED;
    case PH_STARTING:
    case PH_RUNNING: return MESH_ST_RUNNING;
    default: return MESH_ST_STOPPING;
    }
}

int drv_find(const mesh_driver *d, const char *name) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++)
        if (d->slots[s] && !d->slots[s]->unloading && strcmp(drv_name(d->slots[s]), name) == 0) return (int)s;
    return -1;
}

void drv_entry(const dslot *x, uint8_t out[MESH_PT_BYTES]) {
    const dimage *i = &x->image;
    mesh_image im = {x->view_off, x->reads_mask, i->hash, i->regs, i->code_bytes, i->rt_abi, x->loads};
    mesh_manifest_encode_entry(&i->manifest, &im, out);
}

static void put32(uint8_t *b, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + (size_t)i] = (uint8_t)(v >> (8 * i)); }

void drv_pipe(const dslot *x, uint8_t b[MESH_PP_BYTES]) {
    memset(b, 0, MESH_PP_BYTES);
    const mesh_manifest_inbox *in = x->image.manifest.inbox;
    if (in) {
        put32(b, MESH_PP_IN_OFF, in->off);
        put32(b, MESH_PP_IN_CAP, in->cap);
        put32(b, MESH_PP_IN_REC, in->rec);
        put32(b, MESH_PP_IN_TYPE, mesh_pipe_type_hash((const uint8_t *)in->ty, strlen(in->ty)));
    }
    put32(b, MESH_PP_FEEDS_MASK, x->feeds_mask);
    put32(b, MESH_PP_FED_MASK, x->fed_mask);
    if (in) memcpy(b + MESH_PP_TYPE_NAME, in->ty, strlen(in->ty));
}

void drv_ext(const dslot *x, uint8_t b[MESH_PX_BYTES]) {
    mesh_manifest_encode_ext(&x->image.manifest, b);
    put32(b, MESH_PX_PER_SM, x->image.per_sm);
    put32(b, MESH_PX_LOCAL, x->image.local);
    put32(b, MESH_PX_EXEC, x->image.exec);
    put32(b, MESH_PX_REG_CAP, x->image.reg_cap);
    uint64_t sc = mesh_kernel_scratch(x->image.kernel);
    put32(b, MESH_PX_SCRATCH, (uint32_t)sc);
    put32(b, MESH_PX_SCRATCH + 4, (uint32_t)(sc >> 32));
}

/* Upgrade/rollback keep the view iff view_abi, view_bytes and the inbox are unchanged. */
bool drv_view_compatible(const mesh_manifest *a, const mesh_manifest *b) {
    if (a->view_abi != b->view_abi || a->view_bytes != b->view_bytes) return false;
    if (!a->inbox || !b->inbox) return !a->inbox && !b->inbox;
    return a->inbox->off == b->inbox->off && a->inbox->cap == b->inbox->cap && a->inbox->rec == b->inbox->rec &&
           strcmp(a->inbox->ty, b->inbox->ty) == 0;
}

void drv_answer(mesh_reply *reply, bool ok, const char *text) {
    if (!reply) return;
    if (ok) mesh_reply_ok(reply, text);
    else mesh_reply_err(reply, text);
}

/* A load with no one to answer (--state, --autoload) failed during startup: fatal. */
void drv_startup_failed(mesh_driver *d, const mesh_reply *reply, const char *e) {
    if (!reply && d->restoring && !d->startup_error) d->startup_error = mesh_xstrdup(e);
}

void drv_image_free(dimage *im) {
    mesh_manifest_free(&im->manifest);
    free(im->dir);
    if (im->kernel) mesh_kernel_drop(im->kernel);
    memset(im, 0, sizeof *im);
}

#define PUSH(arr, n, cap, v)                                                          \
    do {                                                                              \
        if ((n) == (cap)) {                                                           \
            (cap) = (cap) ? 2 * (cap) : 8;                                            \
            (arr) = mesh_xrealloc((arr), (cap) * sizeof *(arr));                      \
        }                                                                             \
        (arr)[(n)++] = (v);                                                           \
    } while (0)

void drv_push_blink(mesh_driver *d, dblink b) { PUSH(d->blinks, d->nblinks, d->cblinks, b); }
void drv_push_job(mesh_driver *d, djob j) { PUSH(d->jobs, d->njobs, d->cjobs, j); }
static void push_waiter(mesh_driver *d, dwaiter w) { PUSH(d->waiters, d->nwaiters, d->cwaiters, w); }

uint32_t mesh_reads_mask(const mesh_manifest *m, const char *const *names, size_t n) {
    for (size_t i = 0; i < m->nreads; i++) if (strcmp(m->reads[i], "*") == 0) return MESH_READS_ALL;
    uint32_t mask = 0;
    for (size_t s = 0; s < n; s++) {
        if (!names[s]) continue;
        for (size_t i = 0; i < m->nreads; i++) if (strcmp(m->reads[i], names[s]) == 0) { mask |= 1u << s; break; }
    }
    return mask;
}

/* ---------------------------------------------------------------- job setup (SetupState) */

mesh_setup_step mesh_setup_state_step(mesh_setup_state *s, uint64_t job, const mj_setup *setup, size_t n) {
    mesh_setup_step r = {MESH_SETUP_READY, 0, 0, NULL};
    if (!s->has_job || s->job != job) {
        mesh_setup_state_reset(s);
        s->has_job = true;
        s->job = job;
    }
    if (s->busy) { r.k = MESH_SETUP_WAIT; return r; }
    if (s->err) { r.k = MESH_SETUP_FAILED; r.err = s->err; return r; }
    if (s->next < n) { r.k = MESH_SETUP_PUBLISH; r.op = setup[s->next].op; r.arg = setup[s->next].arg; }
    return r;
}

void mesh_setup_state_published(mesh_setup_state *s) {
    s->next++;
    s->busy = true;
}

void mesh_setup_state_completed(mesh_setup_state *s, uint64_t job, const char *err) {
    if (!s->has_job || s->job != job) return;
    s->busy = false;
    if (!s->err && err) s->err = mesh_xstrdup(err);
}

void mesh_setup_state_reset(mesh_setup_state *s) {
    free(s->err);
    memset(s, 0, sizeof *s);
}

/* ---------------------------------------------------------------- construction, startup */

mesh_driver *mesh_driver_new(mesh_shared *sh, mesh_backend *gpu, mesh_backend **cpus, size_t ncpus, mesh_driver_config cfg) {
    mesh_driver *d = mesh_xcalloc(1, sizeof *d);
    mesh_mailbox_set(sh->mb, MESH_H_MAGIC, MESH_MAGIC);
    mesh_mailbox_set(sh->mb, MESH_H_VERSION, MESH_VERSION);
    d->sh = sh;
    d->gpu = gpu;
    d->cpus = mesh_xcalloc(ncpus ? ncpus : 1, sizeof *d->cpus);
    for (size_t i = 0; i < ncpus; i++) d->cpus[i] = cpus[i];
    d->ncpus = ncpus;
    d->cfg = cfg;
    int64_t now = mesh_mono_ns();
    d->last_stats = d->last_log = now;
    mesh_view_heap_init(&d->heap);
    d->restoring = true;
    d->me = sh->me;
    d->n = sh->n;
    return d;
}

/* The executors' kinds, the GPU's first. */
static size_t execs(const mesh_driver *d, uint32_t *out) {
    size_t n = 0;
    out[n++] = drv_gpu_kind(d);
    for (size_t i = 0; i < d->ncpus; i++) out[n++] = d->cpus[i]->ops->caps(d->cpus[i])->kind;
    return n;
}

static bool startup_loads(mesh_driver *d, char **err) {
    if (d->cfg.skip_startup) {
        mesh_info("startup: the fleet's committed desired state decides what loads (--state / --autoload skipped)");
        return true;
    }
    char **names = NULL;
    size_t nnames = 0, cnames = 0;
    if (d->cfg.state) {
        mesh_saved *set;
        size_t n;
        if (!mesh_state_read(d->cfg.state, &set, &n, err)) return false;
        for (size_t i = 0; i < n; i++) {
            mesh_info("--state: %s from %s (%s)", set[i].name, set[i].dir, set[i].running ? "running" : "stopped");
            PUSH(names, nnames, cnames, mesh_xstrdup(set[i].name));
            djob j = {false, mesh_src_catalog(set[i].dir), set[i].running ? 1 : 0, 0, NULL};
            drv_push_job(d, j);
        }
        mesh_state_free(set, n);
    }
    if (d->cfg.autoload) {
        uint32_t ex[8];
        size_t nex = execs(d, ex);
        mesh_buf me = MESH_BUF_INIT;
        for (size_t i = 0; i < nex; i++) mesh_buf_printf(&me, "%s%s", i ? ", " : "", MESH_BACKENDS[ex[i]]);
        char **cat;
        size_t ncat;
        if (!mesh_catalog(d->sh->programs, &cat, &ncat, err)) {
            mesh_buf_free(&me);
            for (size_t i = 0; i < nnames; i++) free(names[i]);
            free(names);
            return false;
        }
        for (size_t c = 0; c < ncat; c++) {
            char *dir = mesh_buf_fmt("%s/%s", d->sh->programs, cat[c]);
            mesh_manifest m;
            char *ptx = NULL, *e = NULL;
            if (!mesh_manifest_load(dir, &m, &ptx, &e)) {
                mesh_error("--autoload: %s", e);
                free(e);
                free(dir);
                continue;
            }
            free(ptx);
            bool known = false;
            for (size_t i = 0; i < nnames; i++) known |= strcmp(names[i], m.name) == 0;
            if (m.autostart && !mesh_runs_here(dir, &m, ex, nex)) {
                mesh_buf b = MESH_BUF_INIT;
                for (size_t i = 0; i < m.nbackends; i++) mesh_buf_printf(&b, "%s%s", i ? ", " : "", m.backends[i]);
                char *suffix = mesh_manifest_native(&m) ? mesh_buf_fmt(", and has no native binary of it for %s", mesh_native_target())
                                                         : mesh_xstrdup("");
                mesh_info("--autoload: %s: not loaded: unsupported here (it runs on %s; this meshd runs %s%s)", m.name, mesh_buf_str(&b),
                          mesh_buf_str(&me), suffix);
                free(suffix);
                mesh_buf_free(&b);
            } else if (m.autostart && !known) {
                mesh_info("--autoload: %s", m.name);
                PUSH(names, nnames, cnames, mesh_xstrdup(m.name));
                djob j = {false, mesh_src_catalog(cat[c]), -1, 0, NULL};
                drv_push_job(d, j);
            }
            mesh_manifest_free(&m);
            free(dir);
        }
        for (size_t c = 0; c < ncat; c++) free(cat[c]);
        free(cat);
        mesh_buf_free(&me);
    }
    for (size_t i = 0; i < nnames; i++) free(names[i]);
    free(names);
    return true;
}

bool mesh_driver_start(mesh_driver *d, char **err) {
    if (!d->gpu->ops->make_current(d->gpu, err)) return false;
    return startup_loads(d, err);
}

/* ---------------------------------------------------------------- the loop */

static void save_state(mesh_driver *d) {
    d->state_dirty = false;
    if (!d->cfg.state) return;
    mesh_saved set[MESH_PROG_SLOTS];
    size_t n = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x || x->unloading) continue;
        set[n].name = (char *)drv_name(x);
        set[n].dir = x->image.dir;
        set[n].running = x->want_run;
        n++;
    }
    char *err = NULL;
    if (!mesh_state_write(d->cfg.state, set, n, &err)) { mesh_error("%s", err); free(err); }
}

/* Something wants prompt attention: a continuous program, a transition, a blink, job work. */
static bool busy(const mesh_driver *d) {
    /* A placed program's frames hop between the machines: see them promptly. */
    if (drv_place_busy(d)) return true;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) if (d->jobw[s].n || d->jobw[s].inflight) return true;
    if (d->nblinks || d->working) return true;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        const dslot *x = d->slots[s];
        if (!x) continue;
        switch (x->phase) {
        case PH_STOPPED: break;
        case PH_RUNNING:
            if (x->image.manifest.policy == MESH_POLICY_CONTINUOUS || !mesh_pring_idle(&x->ring)) return true;
            break;
        default: return true;
        }
    }
    return false;
}

static void commands(mesh_driver *d) {
    mesh_cmd c;
    while (mesh_chan_try_recv(d->sh->cmd, &c) == MESH_CHAN_OK) {
        drv_program_cmd(d, &c);
        mesh_cmd_free(&c);
    }
}

/* Per member over the last second: exchange RTT, wait per round, rounds received (S_MEMBERS). */
static void member_stats(mesh_driver *d, float dt, float *rtt_out, uint32_t *cnt_out, float *worst_out) {
    mesh_rtt_acc rtt[MESH_MAX_SYSTEMS], wait[MESH_MAX_SYSTEMS];
    pthread_mutex_lock(&d->sh->mu);
    memcpy(rtt, d->sh->rtt, sizeof rtt);
    memcpy(wait, d->sh->wait, sizeof wait);
    memset(d->sh->rtt, 0, sizeof d->sh->rtt);
    memset(d->sh->wait, 0, sizeof d->sh->wait);
    pthread_mutex_unlock(&d->sh->mu);
    mesh_host_block *h = &d->sh->host;
    float sum = 0, worst = 0;
    uint32_t cnt = 0;
    for (uint32_t k = 0; k < d->n && k < MESH_MAX_SYSTEMS; k++) {
        size_t m = mesh_member(k);
        float r = rtt[k].n ? (float)(rtt[k].sum / rtt[k].n) : 0.0f, w = wait[k].n ? (float)(wait[k].sum / wait[k].n) : 0.0f;
        mesh_host_block_set_f32(h, m + MESH_M_RTT_US, r);
        mesh_host_block_set_f32(h, m + MESH_M_WAIT_US, w);
        float per = (float)wait[k].n / (dt > 1e-3f ? dt : 1e-3f);
        mesh_host_block_set_u32(h, m + MESH_M_ROUNDS, (uint32_t)(per + 0.5f));
        uint64_t round;
        if (k == d->me) {
            uint32_t rank = mesh_mailbox_get(d->sh->mb, MESH_H_GROUP_RANK);
            round = mesh_mailbox_get64(d->sh->mb, mesh_ex_counter(rank < MESH_MAX_SYSTEMS - 1 ? rank : MESH_MAX_SYSTEMS - 1));
        } else round = atomic_load(&d->sh->round_of[k]);
        mesh_host_block_set_u64(h, m + MESH_M_ROUND, round);
        if (rtt[k].n) { sum += r; cnt++; }
        if (w > worst) worst = w;
    }
    /* Placed programs: the measured time per part (for `split auto`). */
    drv_place_part_ns(d);
    *rtt_out = cnt ? sum / (float)cnt : 0.0f;
    *cnt_out = cnt;
    *worst_out = worst;
}

static void stats(mesh_driver *d) {
    int64_t now = mesh_mono_ns();
    float dt = (float)((double)(now - d->last_stats) / 1e9);
    d->last_stats = now;
    float rtt, wait;
    uint32_t n;
    member_stats(d, dt, &rtt, &n, &wait);
    float total = 0, rates[MESH_PROG_SLOTS] = {0};
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x) continue;
        uint32_t done = mesh_mailbox_get(d->sh->mb, mesh_prog_ctl(s) + MESH_P_DONE);
        x->rate = (float)(done - x->last_done) / dt;
        x->last_done = done;
        rates[s] = x->rate;
        total += x->rate;
    }
    mesh_backend_sample(d->gpu);
    for (size_t i = 0; i < d->ncpus; i++) mesh_backend_sample(d->cpus[i]);
    mesh_host_block *h = &d->sh->host;
    uint8_t rb[4 * MESH_PROG_SLOTS];
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) memcpy(rb + 4 * s, &rates[s], 4);
    mesh_host_field f = {MESH_S_RATES, rb, sizeof rb};
    mesh_host_block_set_many(h, &f, 1);
    mesh_host_block_set_f32(h, MESH_S_TASKS_PER_S, total);
    mesh_host_block_set_f32(h, MESH_S_PEER_RTT_US, rtt);
    mesh_host_block_set_f32(h, MESH_S_LOCK_WAIT_US, wait);
    uint32_t progs = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) progs += d->slots[s] != NULL;
    mesh_host_block_set_u32(h, MESH_S_PROGRAMS, progs);
    mesh_host_block_set_u32(h, MESH_S_HEAP_FREE, mesh_view_heap_free_pages(&d->heap));
    mesh_host_block_set_u32(h, MESH_S_HEAP_RUN, mesh_view_heap_largest_run(&d->heap));
    if (now - d->last_log >= 10000000000ll) {
        d->last_log = now;
        mesh_buf p = MESH_BUF_INIT;
        int first = 1;
        for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
            dslot *x = d->slots[s];
            if (!x) continue;
            mesh_buf_printf(&p, "%s%s %s ", first ? "" : ", ", drv_name(x), MESH_STATES[drv_state(x)]);
            mesh_rs_fixed(&p, x->rate, 0);
            mesh_buf_puts(&p, "/s");
            first = 0;
        }
        mesh_buf line = MESH_BUF_INIT;
        mesh_rs_fixed(&line, total, 0);
        mesh_buf_printf(&line, " tasks/s: %s (epoch %u, peer state %u", first ? "no programs" : mesh_buf_str(&p), d->epoch,
                        atomic_load(&d->sh->peer_state));
        if (n > 0) {
            mesh_buf_puts(&line, ", exchange rtt ");
            mesh_rs_fixed(&line, rtt, 0);
            mesh_buf_puts(&line, " us, slowest member's wait ");
            mesh_rs_fixed(&line, wait, 0);
            mesh_buf_puts(&line, " us/round");
        }
        mesh_buf_putc(&line, ')');
        mesh_info("%s", mesh_buf_str(&line));
        mesh_buf_free(&line);
        mesh_buf_free(&p);
        drv_place_log(d);
    }
}

/* A native slot's worker words (P_PID .. P_LIMITS) every tick, and its CPU time and RSS for the load sampler. */
void drv_native_words(mesh_driver *d, uint32_t s);
void drv_native_words(mesh_driver *d, uint32_t s) {
    dslot *x = d->slots[s];
    if (!x || !mesh_kernel_native(x->image.kernel)) return;
    mesh_native_stats st = mesh_native_worker_stats(mesh_kernel_native(x->image.kernel)), pst = {0};
    bool prev = x->has_prev && mesh_kernel_native(x->prev.kernel);
    if (prev) pst = mesh_native_worker_stats(mesh_kernel_native(x->prev.kernel));
    mesh_mailbox mb = d->sh->mb;
    size_t c = mesh_prog_ctl(s);
    mesh_mailbox_set(mb, c + MESH_P_PID, (uint32_t)st.pid);
    mesh_mailbox_set(mb, c + MESH_P_SPAWNS, st.spawns);
    mesh_mailbox_set(mb, c + MESH_P_CRASHES, st.crashes);
    mesh_mailbox_set(mb, c + MESH_P_EXIT, st.exit);
    mesh_mailbox_set(mb, c + MESH_P_LIMITS, st.limits);
    /* The slot's CPU counter is monotonic across images: add this image's growth. */
    uintptr_t key = (uintptr_t)mesh_kernel_native(x->image.kernel);
    if (d->native_cpu[s].key != key) { d->native_cpu[s].key = key; d->native_cpu[s].cpu = 0; }
    mesh_slot_use *u = &mesh_cpuuse_slots[s];
    mesh_slot_use_add_cpu(u, st.cpu_ns > d->native_cpu[s].cpu ? st.cpu_ns - d->native_cpu[s].cpu : 0);
    if (st.cpu_ns > d->native_cpu[s].cpu) d->native_cpu[s].cpu = st.cpu_ns;
    atomic_store_explicit(&u->rss, st.rss + (prev ? pst.rss : 0), memory_order_relaxed);
    atomic_store_explicit(&u->units, (uint32_t)(st.pid != 0) + (uint32_t)(prev && pst.pid != 0), memory_order_relaxed);
}

static bool all_idle(const mesh_driver *d) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) if (d->slots[s] && !mesh_pring_idle(&d->slots[s]->ring)) return false;
    return true;
}

static int shutdown_(mesh_driver *d) {
    mesh_info("signal: quiescing");
    mesh_mailbox mb = d->sh->mb;
    int64_t deadline = mesh_mono_ns() + 400000000ll;
    while (!all_idle(d) && mesh_mono_ns() < deadline) {
        for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
            if (!d->slots[s]) continue;
            mesh_done *done;
            size_t n;
            mesh_pring_collect(&d->slots[s]->ring, mb, &done, &n);
            for (size_t i = 0; i < n; i++) mesh_reply_drop(done[i].out.kind.reply);
            free(done);
        }
        mesh_sleep_us(TICK_NS / 1000);
    }
    uint32_t left[MESH_PROG_SLOTS];
    size_t nleft = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++)
        if (d->slots[s] && drv_live(d->slots[s])) {
            mesh_mailbox_publish(mb, mesh_prog_ctl(s) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_EXIT);
            left[nleft++] = s;
        }
    deadline = mesh_mono_ns() + 500000000ll;
    while (nleft && mesh_mono_ns() < deadline) {
        size_t k = 0;
        for (size_t i = 0; i < nleft; i++) {
            bool h = true;
            char *e = NULL;
            if (!drv_halted(d, left[i], &h, &e)) { free(e); h = true; }
            if (!h) left[k++] = left[i];
        }
        nleft = k;
        mesh_sleep_us(100);
    }
    if (!d->restoring) save_state(d);
    if (!nleft) { mesh_info("programs halted, exiting"); return 0; }
    mesh_buf b = MESH_BUF_INIT;
    mesh_buf_putc(&b, '[');
    for (size_t i = 0; i < nleft; i++) mesh_buf_printf(&b, "%s%u", i ? ", " : "", left[i]);
    mesh_buf_putc(&b, ']');
    mesh_error("shutdown: programs in slots %s did not halt", mesh_buf_str(&b));
    mesh_buf_free(&b);
    return 1;
}

int mesh_driver_run(mesh_driver *d) {
    for (;;) {
        if (atomic_load(d->sh->quit)) return shutdown_(d);
        int64_t now = mesh_mono_ns();
        commands(d);
        drv_programs_tick(d, now);
        drv_jobs_tick(d);
        drv_blink_tick(d);
        if (d->startup_error) {
            mesh_error("FAILED startup load (%s): --state is left as it was; exiting", d->startup_error);
            free(d->startup_error);
            d->startup_error = NULL;
            shutdown_(d);
            return 1;
        }
        if (mesh_mono_ns() - d->last_stats >= 1000000000ll) stats(d);
        if (d->restoring && !d->njobs && !d->working && !d->nblinks) {
            d->restoring = false;
            d->state_dirty = true;
            atomic_store(&d->sh->ready, true);
            size_t n = 0;
            for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) n += d->slots[s] != NULL;
            mesh_info("startup loads done: %zu program(s) loaded", n);
        }
        if (!d->restoring && d->state_dirty) save_state(d);
        mesh_sleep_us((busy(d) ? FAST_TICK_NS : TICK_NS) / 1000);
    }
}

void mesh_driver_free(mesh_driver *d) {
    if (!d) return;
    drv_place_free_all(d);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        djobslot *j = &d->jobw[s];
        for (size_t i = 0; i < j->n; i++) drv_lease_free(&j->q[i]);
        free(j->q);
        mesh_setup_state_reset(&j->setup);
        if (!x) continue;
        mesh_backend *be = drv_be(d, x->image.exec);
        char *e = NULL;
        if (x->image.kernel && !be->ops->unload(be, x->image.kernel, &e)) free(e);
        x->image.kernel = NULL;
        if (x->has_prev && x->prev.kernel && !drv_be(d, x->prev.exec)->ops->unload(drv_be(d, x->prev.exec), x->prev.kernel, &e)) free(e);
        x->prev.kernel = NULL;
        drv_image_free(&x->image);
        if (x->has_prev) drv_image_free(&x->prev);
        mesh_stream_drop(x->stream);
        mesh_pring_free(&x->ring);
        for (size_t i = 0; i < x->ntasks; i++) mesh_reply_drop(x->tasks[i].reply);
        for (size_t i = 0; i < x->nreqs; i++) { mesh_reply_drop(x->reqs[i].reply); free(x->reqs[i].bytes); }
        free(x->tasks);
        free(x->reqs);
        free(x->failed);
        free(x);
    }
    for (size_t i = 0; i < d->nzombies; i++) {
        char *e = NULL;
        if (d->zombies[i].kernel && !drv_be(d, d->zombies[i].exec)->ops->unload(drv_be(d, d->zombies[i].exec), d->zombies[i].kernel, &e)) free(e);
    }
    free(d->zombies);
    for (size_t i = 0; i < d->njobs; i++) { mesh_src_free(&d->jobs[i].src); mesh_reply_drop(d->jobs[i].reply); }
    free(d->jobs);
    if (d->working) {
        /* The JIT thread still holds its sender: let go of our end. */
        mesh_reply_release(d->work_rx);
        mesh_src_free(&d->work_job.src);
        mesh_reply_drop(d->work_job.reply);
    }
    for (size_t i = 0; i < d->nblinks; i++) {
        if (d->blinks[i].p) { mesh_prepared_free(d->blinks[i].p); free(d->blinks[i].p); }
        mesh_reply_drop(d->blinks[i].reply);
    }
    free(d->blinks);
    for (size_t i = 0; i < d->nwaiters; i++) mesh_reply_drop(d->waiters[i].reply);
    free(d->waiters);
    if (d->gpu_load) mesh_reply_release(d->gpu_load_rx);
    free(d->dead);
    free(d->fault);
    free(d->startup_error);
    mesh_backend_destroy(d->gpu);
    for (size_t i = 0; i < d->ncpus; i++) mesh_backend_destroy(d->cpus[i]);
    free(d->cpus);
    free(d);
}

/* ---------------------------------------------------------------- commands */

static char *unknown(const char *n) {
    mesh_buf b = MESH_BUF_INIT;
    mesh_buf_puts(&b, "no program ");
    mesh_rs_debug_cstr(&b, n);
    mesh_buf_puts(&b, " (`programs` lists them)");
    return mesh_buf_take(&b);
}

static void err_unknown(mesh_reply **reply, const char *n) {
    char *e = unknown(n);
    mesh_reply_err(*reply, e);
    *reply = NULL;
    free(e);
}

static mesh_reply *take(mesh_reply **r) {
    mesh_reply *x = *r;
    *r = NULL;
    return x;
}

static void cmd_unload(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    mesh_info("unload %s (slot %d)%s", c->name, s, c->now ? " --now" : "");
    dslot *x = d->slots[s];
    x->unloading = true;
    x->want_run = false;
    mesh_mailbox_set(d->sh->mb, mesh_prog_ctl((uint32_t)s) + MESH_P_RUN, 0);
    dwaiter w = {(uint32_t)s, false, true, c->now, take(&c->reply), mesh_mono_ns()};
    push_waiter(d, w);
    if (!drv_live(x)) drv_remove(d, (uint32_t)s);
}

static void cmd_prog_run(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    char *why = drv_placed_here(d, (uint32_t)s);
    if (why) { mesh_reply_err(take(&c->reply), why); free(why); return; }
    /* P_RUN is the host's word; GPU requests come through the GPU-owned P_RUN_REQ. */
    mesh_mailbox_set(d->sh->mb, mesh_prog_ctl((uint32_t)s) + MESH_P_RUN, c->on);
    dslot *x = d->slots[s];
    if (d->fault && drv_exec_of(d, (uint32_t)s) == drv_gpu_kind(d)) {
        mesh_reply_errf(take(&c->reply), "the CUDA context is lost (%s); restart meshd", d->fault);
        return;
    }
    if (c->on && x->failed) {
        mesh_info("start %s: clearing FAILED (%s)", c->name, x->failed);
        free(x->failed);
        x->failed = NULL;
    }
    if (x->want_run != c->on) mesh_info("%s %s", c->on ? "start" : "stop", c->name);
    x->want_run = c->on;
    bool done = c->on ? x->phase == PH_RUNNING : x->phase == PH_STOPPED;
    if (done) mesh_reply_ok(take(&c->reply), "0 us");
    else {
        dwaiter w = {(uint32_t)s, c->on, false, false, take(&c->reply), mesh_mono_ns()};
        push_waiter(d, w);
    }
    d->state_dirty = true;
}

static void cmd_task(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    dslot *x = d->slots[s];
    if (!x->want_run || x->failed) mesh_reply_errf(take(&c->reply), "%s is not running", c->name);
    else if (x->ntasks >= MAX_QUEUED) mesh_reply_errf(take(&c->reply), "%s: %d tasks queued already", c->name, MAX_QUEUED);
    else {
        dtask t = {c->op, c->arg, take(&c->reply)};
        PUSH(x->tasks, x->ntasks, x->ctasks, t);
    }
}

static void cmd_req(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    dslot *x = d->slots[s];
    const mesh_manifest *m = &x->image.manifest;
    /* Word writes never pass req_max: a body that does not fit is refused, never cut. */
    size_t max = m->has_request ? (m->request.req_max & ~3u) : 0;
    if (!m->has_request) mesh_reply_errf(take(&c->reply), "%s declares no request window", c->name);
    else if (c->len > max) mesh_reply_errf(take(&c->reply), "%s: request of %zu bytes > its req_max %zu", c->name, c->len, max);
    else if (!x->want_run || x->failed) mesh_reply_errf(take(&c->reply), "%s is not running", c->name);
    else if (x->nreqs >= MAX_QUEUED) mesh_reply_errf(take(&c->reply), "%s: %d requests queued already", c->name, MAX_QUEUED);
    else {
        dreq r = {c->bytes, c->len, take(&c->reply)};
        c->bytes = NULL;
        PUSH(x->reqs, x->nreqs, x->creqs, r);
    }
}

static void cmd_input(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    /* Placed: the input goes to the machine that applies it (pin k: k; split: the owner). */
    uint32_t to = drv_input_to(d, (uint32_t)s);
    char *text = NULL;
    bool ok;
    if (to == d->me) ok = drv_input(d, (uint32_t)s, c->bytes, c->len, &text) == 0;
    else if (c->len > MESH_F_INPUT_MAX) ok = false, text = mesh_buf_fmt("%s: at most %d bytes forwarded at once", c->name, MESH_F_INPUT_MAX);
    else {
        mpw_bytes f = mpw_input_encode(c->name, c->bytes, c->len);
        ok = mesh_shared_send_peer(d->sh, to, f.b, f.n);
        mpw_bytes_free(&f);
        text = ok ? mesh_buf_fmt("%zu bytes to system %u", c->len, to) : mesh_buf_fmt("peer: the cluster is not up; %s's input goes to system %u", c->name, to);
    }
    drv_answer(take(&c->reply), ok, text);
    free(text);
}

static void cmd_view_get(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    dslot *x = d->slots[s];
    if (drv_live(x)) { mesh_reply_errf(take(&c->reply), "%s is running here: a view is taken from a stopped program", c->name); return; }
    mesh_view_bytes *v = mesh_xcalloc(1, sizeof *v);
    v->view_abi = x->image.manifest.view_abi;
    v->len = x->image.manifest.view_bytes;
    v->bytes = mesh_xmalloc(v->len ? v->len : 1);
    mesh_mailbox_read(d->sh->mb, x->view_off, v->bytes, v->len);
    mesh_reply_ptr(take(&c->reply), v, mesh_view_bytes_free);
}

static void cmd_view_put(mesh_driver *d, mesh_cmd *c) {
    int s = drv_find(d, c->name);
    if (s < 0) { err_unknown(&c->reply, c->name); return; }
    dslot *x = d->slots[s];
    const mesh_manifest *m = &x->image.manifest;
    if (drv_live(x)) { mesh_reply_errf(take(&c->reply), "%s is running here: a view is written into a stopped program", c->name); return; }
    if (m->view_abi != c->view_abi || m->view_bytes != c->len) {
        mesh_reply_errf(take(&c->reply), "view: the source's is view_abi %u of %zu bytes, this image's view_abi %u of %u bytes", c->view_abi, c->len,
                        m->view_abi, m->view_bytes);
        return;
    }
    mesh_mailbox_write(d->sh->mb, x->view_off, c->bytes, c->len, c->len);
    /* It holds a live program's state now: no OP_INIT over it. */
    x->needs_init = false;
    mesh_reply_okf(take(&c->reply), "%zu bytes", c->len);
}

static void cmd_job_lease(mesh_driver *d, mesh_cmd *c) {
    mj_lease_msg *l = &c->lease;
    int s = drv_find(d, l->program);
    bool ok = false;
    if (s >= 0) {
        dslot *x = d->slots[s];
        ok = x->image.manifest.policy == MESH_POLICY_JOB && x->want_run && !x->failed && !x->unloading;
    }
    if (ok) {
        dlease q;
        q.lease = l->lease;
        q.job = l->job;
        q.op = l->op;
        q.setup = l->setup;
        q.nsetup = l->nsetup;
        q.items = l->items;
        free(l->program); /* the queue keeps no name: the slot is the program */
        memset(l, 0, sizeof *l);
        djobslot *j = &d->jobw[s];
        PUSH(j->q, j->n, j->cap, q);
    } else {
        char *why = mesh_buf_fmt("%s does not run here", l->program);
        dlease tmp = {l->lease, l->job, l->op, NULL, 0, l->items};
        drv_jobs_returned(d, &tmp, MESH_JOBS_RS_RETURNED, why);
        free(why);
    }
}

static void cmd_job_cut(mesh_driver *d, mesh_cmd *c) {
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        djobslot *j = &d->jobw[s];
        size_t k = 0;
        for (size_t i = 0; i < j->n; i++) {
            if (j->q[i].lease == c->lease_id) mj_ranges_remove(&j->q[i].items, c->cut, UINT64_MAX);
            if (mj_ranges_is_empty(&j->q[i].items)) drv_lease_free(&j->q[i]);
            else j->q[k++] = j->q[i];
        }
        j->n = k;
    }
}

static void cmd_job_progs(mesh_driver *d, mesh_cmd *c) {
    mesh_job_progs *p = mesh_xcalloc(1, sizeof *p);
    p->v = mesh_xcalloc(MESH_PROG_SLOTS, sizeof *p->v);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x || x->image.manifest.policy != MESH_POLICY_JOB || !x->want_run || x->failed || x->unloading || x->phase != PH_RUNNING) continue;
        mj_prog *q = &p->v[p->n++];
        q->name = mesh_xstrdup(drv_name(x));
        q->exec = x->image.exec;
        q->cap = drv_job_cap(&x->image.manifest, x->image.exec);
    }
    mesh_reply_ptr(take(&c->reply), p, mesh_job_progs_free);
}

static void cmd_inventory(mesh_driver *d, mesh_cmd *c) {
    mesh_actuals *a = mesh_xcalloc(1, sizeof *a);
    a->v = mesh_xcalloc(MESH_PROG_SLOTS, sizeof *a->v);
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x || x->unloading) continue;
        mesh_actual *y = &a->v[a->n++];
        y->name = mesh_xstrdup(drv_name(x));
        y->dir = mesh_xstrdup(x->image.dir);
        y->hash = x->image.hash;
        y->has_prev = x->has_prev;
        y->prev = x->has_prev ? x->prev.hash : 0;
        y->run = x->want_run || drv_place_stopped(d, drv_name(x));
        y->failed = x->failed != NULL;
        y->lockstep = x->image.manifest.peer == MESH_PEER_MODE_LOCKSTEP;
    }
    mesh_reply_ptr(take(&c->reply), a, mesh_actuals_free);
}

void drv_program_cmd(mesh_driver *d, mesh_cmd *c) {
    switch (c->k) {
    case MESH_CMD_LOAD: {
        djob j = {false, c->src, c->run, 0, take(&c->reply)};
        c->has_src = false;
        memset(&c->src, 0, sizeof c->src);
        drv_push_job(d, j);
        break;
    }
    case MESH_CMD_UPGRADE: {
        int s = drv_find(d, c->name);
        if (s < 0) { err_unknown(&c->reply, c->name); break; }
        djob j;
        memset(&j, 0, sizeof j);
        j.upgrade = true;
        j.slot = (uint32_t)s;
        j.reply = take(&c->reply);
        if (c->has_src) { j.src = c->src; c->has_src = false; memset(&c->src, 0, sizeof c->src); }
        else j.src = mesh_src_catalog(d->slots[s]->image.dir);
        drv_push_job(d, j);
        break;
    }
    case MESH_CMD_ROLLBACK: {
        int s = drv_find(d, c->name);
        if (s < 0) { err_unknown(&c->reply, c->name); break; }
        if (!d->slots[s]->has_prev) { mesh_reply_errf(take(&c->reply), "%s: no previous image to roll back to", c->name); break; }
        dblink b = {OP_ROLLBACK, NULL, -1, (uint32_t)s, take(&c->reply), true, (uint32_t)s, drv_exec_of(d, (uint32_t)s), mesh_mono_ns()};
        drv_push_blink(d, b);
        break;
    }
    case MESH_CMD_RESET: {
        int s = drv_find(d, c->name);
        if (s < 0) { err_unknown(&c->reply, c->name); break; }
        char *why = drv_placed_here(d, (uint32_t)s);
        if (why) { mesh_reply_err(take(&c->reply), why); free(why); break; }
        if (!drv_live(d->slots[s])) {
            char *t = NULL;
            bool ok = drv_op_reset(d, (uint32_t)s, &t);
            if (ok) { free(t); t = mesh_xstrdup("blink 0"); }
            drv_answer(take(&c->reply), ok, t);
            free(t);
        } else {
            dblink b = {OP_RESET, NULL, -1, (uint32_t)s, take(&c->reply), true, (uint32_t)s, drv_exec_of(d, (uint32_t)s), mesh_mono_ns()};
            drv_push_blink(d, b);
        }
        break;
    }
    case MESH_CMD_UNLOAD: cmd_unload(d, c); break;
    case MESH_CMD_PROG_RUN: cmd_prog_run(d, c); break;
    case MESH_CMD_TASK: cmd_task(d, c); break;
    case MESH_CMD_REQ: cmd_req(d, c); break;
    case MESH_CMD_INPUT: cmd_input(d, c); break;
    case MESH_CMD_PLACE:
        if (drv_find(d, c->name) < 0) err_unknown(&c->reply, c->name);
        else drv_cmd_place(d, c, take(&c->reply));
        break;
    case MESH_CMD_PEER_PLACE: drv_cmd_peer_place(d, c); break;
    case MESH_CMD_PEER_FRAME: drv_cmd_peer_frame(d, c); break;
    case MESH_CMD_PEER_INPUT: drv_cmd_peer_input(d, c); break;
    case MESH_CMD_FLEET_VIEW: drv_cmd_fleet_view(d, c); break;
    case MESH_CMD_INVENTORY: cmd_inventory(d, c); break;
    case MESH_CMD_VIEW_GET: cmd_view_get(d, c); break;
    case MESH_CMD_VIEW_PUT: cmd_view_put(d, c); break;
    case MESH_CMD_JOB_LEASE: cmd_job_lease(d, c); break;
    case MESH_CMD_JOB_CUT: cmd_job_cut(d, c); break;
    case MESH_CMD_JOB_PROGS: cmd_job_progs(d, c); break;
    }
}
