/* Loads and upgrades: images compiled on a JIT thread, then blinks (halt what the change touches, the
 * module operations, relaunch); rollback, reset; the table rewritten; the occupancy budget. */
#include "driver_int.h"

#include "mesh/native.h"
#include "mesh/occupancy.h"
#include "mesh/rsfmt.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PUSH(arr, n, cap, v)                                                          \
    do {                                                                              \
        if ((n) == (cap)) {                                                           \
            (cap) = (cap) ? 2 * (cap) : 8;                                            \
            (arr) = mesh_xrealloc((arr), (cap) * sizeof *(arr));                      \
        }                                                                             \
        (arr)[(n)++] = (v);                                                           \
    } while (0)

static double ms(int64_t ns) { return (double)ns / 1e6; }

/* What an executor compiles a program to (for logs). */
static const char *code_word(uint32_t exec) {
    switch (exec) {
    case MESH_BACKEND_METAL: return "MSL";
    case MESH_BACKEND_WASM: return "wasm";
    case MESH_BACKEND_NATIVE: return "native binary";
    default: return "cubin";
    }
}

static bool module_scope(mesh_driver *d, uint32_t exec, bool has_slot, uint32_t slot, uint32_t *scope) {
    mesh_backend *be = drv_be(d, exec);
    return be->ops->module_scope(be, has_slot, slot, scope);
}

/* ---------------------------------------------------------------- the JIT thread */

typedef struct {
    mesh_compiler_entry comps[8];
    size_t ncomps;
    char *root;
    mesh_src src;
    int warm_run; /* -1: the manifest's autostart */
    mesh_reply *tx;
} jit_job;

static void prepared_free_heap(void *p) {
    if (!p) return;
    mesh_prepared_free(p);
    free(p);
}

static void *jit(void *arg) {
    jit_job *j = arg;
    mesh_payload p;
    char *err = NULL;
    bool ok;
    mesh_prepared *out = mesh_xcalloc(1, sizeof *out);
    if (!j->src.is_bytes) {
        ok = mesh_loader_read(j->root, j->src.dir, false, &p, &err) && mesh_prepare_any(j->comps, j->ncomps, &p, NULL, j->root, out, &err);
    } else {
        mesh_payload_copy(&p, &j->src.payload);
        ok = mesh_prepare_any(j->comps, j->ncomps, &p, &j->src.hash, j->root, out, &err);
    }
    /* A native image that will run at once gets its process started here, off the driver thread. */
    if (ok && out->kernel && mesh_kernel_native(out->kernel)) {
        bool run = j->warm_run < 0 ? out->manifest.autostart : j->warm_run == 1;
        if (run && !mesh_native_worker_warm(mesh_kernel_native(out->kernel), &err)) { mesh_prepared_free(out); ok = false; }
    }
    if (ok) mesh_reply_ptr(j->tx, out, prepared_free_heap);
    else {
        free(out);
        mesh_reply_err(j->tx, err ? err : "jit: failed");
    }
    free(err);
    free(j->root);
    mesh_src_free(&j->src);
    free(j);
    return NULL;
}

/* The job a load or upgrade is (for log lines). */
static char *what(const mesh_src *s) { return mesh_src_what(s); }

bool drv_precheck(mesh_driver *d, const mesh_manifest *m, char **err) {
    if (drv_find(d, m->name) >= 0) { *err = mesh_buf_fmt("name exists: %s is loaded", m->name); return false; }
    if (!drv_pipe_check(d, m, false, 0, err)) return false;
    /* The exchange page has one owner. */
    if (m->peer == MESH_PEER_MODE_LOCKSTEP) {
        for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
            dslot *x = d->slots[s];
            if (x && x->image.manifest.peer == MESH_PEER_MODE_LOCKSTEP) {
                *err = mesh_buf_fmt("manifest: %s is the lockstep program already; only one may be loaded", drv_name(x));
                return false;
            }
        }
    }
    bool full = true;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) full &= d->slots[s] != NULL;
    if (full) { *err = mesh_buf_fmt("no slot: all %d slots are loaded", MESH_PROG_SLOTS); return false; }
    mesh_view_heap h = d->heap;
    for (size_t i = 0; i < d->nzombies; i++) mesh_view_heap_free(&h, d->zombies[i].view_off, d->zombies[i].view_bytes);
    uint32_t off;
    if (!mesh_view_heap_alloc(&h, m->view_bytes, &off)) {
        *err = mesh_buf_fmt("view heap: %zu pages wanted, %u free (fragmented)", mesh_view_pages(m->view_bytes), mesh_view_heap_free_pages(&h));
        return false;
    }
    return true;
}

void drv_jobs_tick(mesh_driver *d) {
    if (!d->working && !d->nblinks && d->njobs) {
        djob job = d->jobs[0];
        memmove(d->jobs, d->jobs + 1, (d->njobs - 1) * sizeof *d->jobs);
        d->njobs--;
        if (d->fault) {
            mesh_reply_errf(job.reply, "the CUDA context is lost (%s); restart meshd", d->fault);
            mesh_src_free(&job.src);
            return;
        }
        jit_job *j = mesh_xcalloc(1, sizeof *j);
        mesh_backend *all[9];
        size_t nall = 0;
        all[nall++] = d->gpu;
        for (size_t i = 0; i < d->ncpus && nall < 8; i++) all[nall++] = d->cpus[i];
        for (size_t i = 0; i < nall; i++) {
            j->comps[i].kind = all[i]->ops->caps(all[i])->kind;
            j->comps[i].comp = all[i]->ops->compiler(all[i]);
        }
        j->ncomps = nall;
        j->root = mesh_xstrdup(d->sh->programs);
        j->src = mesh_src_copy(&job.src);
        j->warm_run = job.upgrade ? 1 : job.run;
        mesh_reply *rx = mesh_reply_new();
        j->tx = mesh_reply_sender(rx);
        pthread_t th;
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &a, jit, j) != 0) {
            mesh_reply_err(j->tx, "jit: the worker thread died");
            free(j->root);
            mesh_src_free(&j->src);
            free(j);
        }
        pthread_attr_destroy(&a);
        d->working = true;
        d->work_rx = rx;
        d->work_job = job;
    }
    if (!d->working) return;
    mesh_result r;
    int got = mesh_reply_poll(d->work_rx, &r);
    if (got == MESH_CHAN_EMPTY) return;
    if (got != MESH_CHAN_OK) r = mesh_result_err("jit: the worker thread died");
    mesh_reply_release(d->work_rx);
    d->working = false;
    d->work_rx = NULL;
    djob job = d->work_job;
    int64_t since = mesh_mono_ns();
    char *w = what(&job.src);
    if (!r.ok) {
        if (job.upgrade) mesh_error("upgrade from %s: %s", w, r.text);
        else {
            mesh_error("load %s: %s", w, r.text);
            char *e = mesh_buf_fmt("%s: %s", w, r.text);
            drv_startup_failed(d, job.reply, e);
            free(e);
        }
        mesh_reply_err(job.reply, r.text);
    } else {
        mesh_prepared *p = r.ptr;
        r.ptr = NULL;
        if (!job.upgrade) {
            char *e = NULL;
            /* Refusals that need no module are answered without a blink. */
            if (!drv_precheck(d, &p->manifest, &e)) {
                mesh_error("load %s: %s", mesh_prepared_dir(p), e);
                char *f = mesh_buf_fmt("%s: %s", mesh_prepared_dir(p), e);
                drv_startup_failed(d, job.reply, f);
                free(f);
                mesh_reply_err(job.reply, e);
                free(e);
                mesh_prepared_free(p);
                free(p);
            } else {
                mesh_buf l = MESH_BUF_INIT;
                mesh_buf_printf(&l, "load %s: JIT ", mesh_prepared_dir(p));
                mesh_rs_fixed(&l, p->compile_ms, 2);
                mesh_buf_printf(&l, " ms, %zu bytes of %s (%s); blinking", p->code_len, code_word(p->exec), MESH_BACKENDS[p->exec]);
                mesh_info("%s", mesh_buf_str(&l));
                mesh_buf_free(&l);
                uint32_t scope = 0;
                bool has = module_scope(d, p->exec, false, 0, &scope);
                dblink b = {OP_LOAD, p, job.run, 0, job.reply, has, scope, p->exec, since};
                drv_push_blink(d, b);
            }
        } else {
            mesh_buf l = MESH_BUF_INIT;
            mesh_buf_printf(&l, "upgrade %s from %s: JIT ", p->manifest.name, mesh_prepared_dir(p));
            mesh_rs_fixed(&l, p->compile_ms, 2);
            mesh_buf_puts(&l, " ms; blinking");
            mesh_info("%s", mesh_buf_str(&l));
            mesh_buf_free(&l);
            /* The slot's executor decides the scope (an image for another one is refused in the blink). */
            uint32_t exec = drv_exec_of(d, job.slot), scope = 0;
            bool has = module_scope(d, exec, true, job.slot, &scope);
            dblink b = {OP_UPGRADE, p, -1, job.slot, job.reply, has, scope, exec, since};
            drv_push_blink(d, b);
        }
    }
    free(w);
    mesh_src_free(&job.src);
    mesh_result_free(&r);
}

/* ---------------------------------------------------------------- the blink */

/* mps: the scope's kernel halts across ticks; true once halted, or overdue (do_blink then fails it). */
static bool halt_scope(mesh_driver *d, bool has, uint32_t scope) {
    if (!has || scope == MESH_NO_SLOT) return true;
    if (!d->slots[scope] || !drv_live(d->slots[scope])) return true;
    if (!d->halting) {
        mesh_mailbox_publish(d->sh->mb, mesh_prog_ctl(scope) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT);
        d->halting = true;
        d->halting_t = mesh_mono_ns();
    }
    bool h = true;
    char *e = NULL;
    if (!drv_halted(d, scope, &h, &e)) { free(e); h = true; }
    return h || mesh_mono_ns() - d->halting_t > HALT_NS;
}

static bool size_stack(mesh_driver *d, char **err) {
    uint32_t need = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x) continue;
        if (x->image.local > need) need = x->image.local;
        if (x->has_prev && x->prev.local > need) need = x->prev.local;
    }
    size_t have;
    if (!d->gpu->ops->stack_limit(d->gpu, &have, err)) return false;
    if (need > have) {
        int64_t t0 = mesh_mono_ns();
        if (!d->gpu->ops->set_stack_limit(d->gpu, need, err)) return false;
        size_t now;
        if (!d->gpu->ops->stack_limit(d->gpu, &now, err)) return false;
        mesh_buf l = MESH_BUF_INIT;
        mesh_buf_printf(&l, "context stack %zu -> %zu bytes per thread (", have, now);
        mesh_rs_fixed(&l, ms(mesh_mono_ns() - t0), 2);
        mesh_buf_puts(&l, " ms)");
        mesh_info("%s", mesh_buf_str(&l));
        mesh_buf_free(&l);
    }
    return true;
}

/* Every halted slot's entry rewritten, reads masks re-resolved by name; the running ones whose mask changed are returned. */
static size_t rewrite_entries(mesh_driver *d, uint32_t *rebind) {
    const char *names[MESH_PROG_SLOTS];
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) names[s] = d->slots[s] ? drv_name(d->slots[s]) : NULL;
    mesh_mailbox mb = d->sh->mb;
    size_t n = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x) continue;
        uint32_t mask = mesh_reads_mask(&x->image.manifest, names, MESH_PROG_SLOTS);
        uint8_t e[MESH_PT_BYTES], ext[MESH_PX_BYTES];
        if (drv_live(x) && mesh_backend_reads_live(drv_be(d, x->image.exec))) {
            /* wasm lanes and native workers read the mask under the seqlock at every peer view: rewritten in place. */
            if (mask != x->reads_mask) {
                x->reads_mask = mask;
                drv_entry(x, e);
                drv_ext(x, ext);
                mesh_pt_write_entry(mb, s, e, ext, drv_state(x));
            }
            continue;
        }
        if (drv_live(x)) {
            if (mask != x->reads_mask) rebind[n++] = s;
            continue;
        }
        x->reads_mask = mask;
        drv_entry(x, e);
        drv_ext(x, ext);
        mesh_pt_write_entry(mb, s, e, ext, drv_state(x));
    }
    drv_connect_pipes(d);
    return n;
}

/* The image's module (blink mode: loaded here) plus the checks that need it. Consumes p. */
static bool load_image(mesh_driver *d, mesh_prepared *p, dimage *out, char **err) {
    uint32_t exec = p->exec, reg_cap = p->reg_cap;
    mesh_backend *be = drv_be(d, exec);
    mesh_kernel *k = NULL;
    char *e = NULL;
    if (!be->ops->load(be, p, &k, &e)) {
        *err = mesh_buf_fmt("load: %s", e);
        free(e);
        mesh_prepared_free(p);
        return false;
    }
    uint32_t rt = 0, regs = 0, per_sm = 0, local = 0;
    bool ok = true;
    if (!mesh_kernel_global_u32(k, MESH_RT_ABI_SYMBOL, &rt, &e)) { *err = mesh_buf_fmt("abi: %s", e); ok = false; }
    else if (rt != MESH_RT_ABI) { *err = mesh_buf_fmt("abi: the image's %s is %u, meshd runs %u", MESH_RT_ABI_SYMBOL, rt, MESH_RT_ABI); ok = false; }
    else if (!mesh_kernel_regs(k, &regs, &e)) { *err = mesh_buf_fmt("load: %s", e); ok = false; }
    else if (!mesh_kernel_max_blocks_per_sm(k, &per_sm, &e)) { *err = mesh_buf_fmt("load: %s", e); ok = false; }
    else if (per_sm == 0) {
        *err = mesh_buf_fmt("regs: %u registers per thread: not one block of mesh_main (%u threads, %u shared bytes) fits an SM", regs,
                            mesh_manifest_threads(&p->manifest), mesh_manifest_smem(&p->manifest));
        ok = false;
    } else if (!mesh_kernel_local_bytes(k, &local, &e)) { *err = mesh_buf_fmt("load: %s", e); ok = false; }
    free(e);
    if (!ok) {
        char *ue = NULL;
        if (!be->ops->unload(be, k, &ue)) free(ue);
        mesh_prepared_free(p);
        return false;
    }
    memset(out, 0, sizeof *out);
    out->manifest = p->manifest;
    memset(&p->manifest, 0, sizeof p->manifest);
    out->dir = mesh_xstrdup(p->src.dir);
    out->kernel = k;
    out->hash = p->hash;
    out->code_bytes = (uint32_t)p->code_len;
    out->regs = regs;
    out->per_sm = per_sm;
    out->reg_cap = reg_cap;
    out->local = local;
    out->rt_abi = rt;
    out->exec = exec;
    mesh_prepared_free(p);
    return true;
}

static void unload_image_kernel(mesh_driver *d, dimage *im) {
    if (!im->kernel) return;
    char *e = NULL;
    if (!drv_be(d, im->exec)->ops->unload(drv_be(d, im->exec), im->kernel, &e)) free(e);
    im->kernel = NULL;
}

/* Co-residency: every loaded program's nodes must fit at once (backend.rs co_resident); nodes = "fit" decided here. */
static bool budget(mesh_driver *d, dimage *im, bool has_replace, uint32_t replace, char **err) {
    if (!drv_be(d, im->exec)->ops->resident(drv_be(d, im->exec))) return true;
    const mesh_caps *caps = d->gpu->ops->caps(d->gpu);
    uint32_t sms = caps->sms, bps = caps->blocks_per_sm;
    mesh_occ_kernel all[MESH_PROG_SLOTS + 1];
    size_t n = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!x || (has_replace && replace == s) || !drv_be(d, x->image.exec)->ops->resident(drv_be(d, x->image.exec))) continue;
        all[n].nodes = x->image.manifest.nodes;
        all[n].per_sm = x->image.per_sm;
        n++;
    }
    mesh_manifest *m = &im->manifest;
    if (m->nodes_fit) {
        /* At most three quarters of the device's capacity for its shape: the rest stays for other programs. */
        uint32_t q = im->per_sm * sms * 3 / 4;
        uint32_t max = MESH_PROG_MAX_NODES < (q > 1 ? q : 1) ? MESH_PROG_MAX_NODES : (q > 1 ? q : 1);
        uint32_t fit = mesh_occ_room(all, n, im->per_sm, max, sms, bps);
        if (fit == 0) {
            *err = mesh_buf_fmt("occupancy: %s (nodes = \"fit\"): no room for one block of %u threads beside the loaded kernels", m->name,
                                mesh_manifest_threads(m));
            return false;
        }
        m->nodes = fit;
    }
    if (m->nodes > im->per_sm * sms) {
        *err = mesh_buf_fmt("occupancy: %s needs %u resident blocks of %u threads; its %u regs and %u shared bytes allow %u per SM x %u SMs", m->name,
                            m->nodes, mesh_manifest_threads(m), im->regs, mesh_manifest_smem(m), im->per_sm, sms);
        return false;
    }
    all[n].nodes = m->nodes;
    all[n].per_sm = im->per_sm;
    char *e = NULL;
    if (!mesh_co_resident(all, n + 1, sms, bps, &e)) {
        *err = mesh_buf_fmt("occupancy: %s: %s", m->name, e);
        free(e);
        return false;
    }
    return true;
}

static void slot_free(dslot *x) {
    drv_image_free(&x->image);
    if (x->has_prev) drv_image_free(&x->prev);
    mesh_stream_drop(x->stream);
    mesh_pring_free(&x->ring);
    free(x->tasks);
    free(x->reqs);
    free(x->failed);
    free(x);
}

static bool op_load(mesh_driver *d, mesh_prepared *p, int run, char **out) {
    char *err = NULL;
    if (!drv_precheck(d, &p->manifest, &err)) { mesh_prepared_free(p); free(p); *out = err; return false; }
    uint32_t s = 0;
    while (d->slots[s]) s++;
    dimage im;
    bool ok = load_image(d, p, &im, &err);
    free(p);
    if (!ok) { *out = err; return false; }
    mesh_backend *ex = drv_be(d, im.exec);
    if (!budget(d, &im, false, 0, &err)) { unload_image_kernel(d, &im); drv_image_free(&im); *out = err; return false; }
    mesh_stream *stream = NULL;
    if (!ex->ops->new_stream(ex, s, im.kernel, &stream, &err)) {
        *out = mesh_buf_fmt("load: %s", err);
        free(err);
        unload_image_kernel(d, &im);
        drv_image_free(&im);
        return false;
    }
    const mesh_manifest *m = &im.manifest;
    uint32_t view_off = 0;
    mesh_view_heap_alloc(&d->heap, m->view_bytes, &view_off); /* prechecked */
    mesh_mailbox mb = d->sh->mb;
    mesh_pt_reset_slot(mb, s);
    /* P_RUN_REQ is the GPU's word, never written here: requests count from its value now. */
    uint32_t run_seq = mesh_mailbox_get(mb, mesh_prog_ctl(s) + MESH_P_RUN_REQ);
    mesh_mailbox_zero(mb, view_off, m->view_bytes);
    if (m->peer == MESH_PEER_MODE_LOCKSTEP) drv_zero_exchange(d);
    bool want_run = run < 0 ? m->autostart : run == 1;
    mesh_mailbox_set(mb, mesh_prog_ctl(s) + MESH_P_RUN, want_run);
    if (mesh_kernel_native(im.kernel)) {
        mesh_info("load %s into slot %u (native): worker %s, %u bytes, hash %016llx, view 0x%x+0x%x, %s", m->name, s,
                  mesh_native_worker_bin(mesh_kernel_native(im.kernel)), im.code_bytes, (unsigned long long)im.hash, view_off, m->view_bytes,
                  want_run ? "starting" : "stopped");
    } else {
        char cap[32] = "";
        if (im.reg_cap > 0) snprintf(cap, sizeof cap, " (cap %u)", im.reg_cap);
        mesh_info("load %s into slot %u (%s): %u nodes of %u warp(s), %u shared bytes, %u regs%s, %u B local, %u blocks/SM, %u bytes of %s, hash "
                  "%016llx, view 0x%x+0x%x, %s",
                  m->name, s, MESH_BACKENDS[im.exec], m->nodes, m->warps, m->shared_bytes, im.regs, cap, im.local, im.per_sm, im.code_bytes,
                  code_word(im.exec), (unsigned long long)im.hash, view_off, m->view_bytes, want_run ? "starting" : "stopped");
    }
    dslot *x = mesh_xcalloc(1, sizeof *x);
    x->image = im;
    x->stream = stream;
    x->view_off = view_off;
    x->loads = 1;
    x->phase = PH_STOPPED;
    x->want_run = want_run;
    x->run_seq = run_seq;
    x->needs_init = true;
    mesh_pring_init(&x->ring, s);
    x->last_auto = mesh_mono_ns();
    d->slots[s] = x;
    d->place[s] = NULL;
    drv_sync_place(d, s);
    *out = mesh_buf_fmt("slot %u", s);
    return true;
}

/* Gives slot s a view for `to` (from `from`): kept iff compatible, else a fresh zeroed one and OP_INIT. */
static bool rebind_view(mesh_driver *d, uint32_t s, const mesh_manifest *from, const mesh_manifest *to, const char **res, char **err) {
    if (drv_view_compatible(from, to)) { *res = "kept"; return true; }
    dslot *x = d->slots[s];
    uint32_t old = x->view_off, off;
    if (!mesh_view_heap_alloc(&d->heap, to->view_bytes, &off)) {
        *err = mesh_buf_fmt("view heap: %zu pages wanted, %u free", mesh_view_pages(to->view_bytes), mesh_view_heap_free_pages(&d->heap));
        return false;
    }
    /* The old view is freed at the next module change, not now: a reader (`reads`) may hold a View on it until then. */
    dzombie z = {NULL, old, from->view_bytes, drv_exec_of(d, s), from->inbox != NULL, x->fed_mask, mesh_mono_ns(), false};
    PUSH(d->zombies, d->nzombies, d->czombies, z);
    size_t k = 0;
    for (size_t i = 0; i < d->ndead; i++) if (d->dead[i].consumer != s) d->dead[k++] = d->dead[i];
    d->ndead = k;
    mesh_mailbox_zero(d->sh->mb, off, to->view_bytes);
    if (to->peer == MESH_PEER_MODE_LOCKSTEP) drv_zero_exchange(d);
    x->view_off = off;
    x->needs_init = true;
    *res = "reset";
    return true;
}

static bool op_upgrade(mesh_driver *d, uint32_t s, mesh_prepared *p, char **out) {
    if (!d->slots[s]) { mesh_prepared_free(p); free(p); *out = mesh_xstrdup("the program was unloaded meanwhile"); return false; }
    const char *name = drv_name(d->slots[s]);
    if (strcmp(p->manifest.name, name) != 0) {
        mesh_buf b = MESH_BUF_INIT;
        mesh_buf_printf(&b, "manifest: %s names ", mesh_prepared_dir(p));
        mesh_rs_debug_cstr(&b, p->manifest.name);
        mesh_buf_puts(&b, ", not ");
        mesh_rs_debug_cstr(&b, name);
        *out = mesh_buf_take(&b);
        mesh_prepared_free(p);
        free(p);
        return false;
    }
    uint32_t exec = drv_exec_of(d, s);
    if (p->exec != exec) {
        *out = mesh_buf_fmt("exec: %s runs on %s here and %s would run on %s (its manifest's `backends`): unload and load it to change executors", name,
                            MESH_BACKENDS[exec], mesh_prepared_dir(p), MESH_BACKENDS[p->exec]);
        if (p->kernel) {
            char *e = NULL;
            if (!drv_be(d, p->exec)->ops->unload(drv_be(d, p->exec), p->kernel, &e)) free(e);
            p->kernel = NULL;
        }
        mesh_prepared_free(p);
        free(p);
        return false;
    }
    dimage im;
    char *err = NULL;
    bool ok = load_image(d, p, &im, &err);
    free(p);
    if (!ok) { *out = err; return false; }
    if (!budget(d, &im, true, s, &err) || !drv_pipe_check(d, &im.manifest, true, s, &err)) {
        unload_image_kernel(d, &im);
        drv_image_free(&im);
        *out = err;
        return false;
    }
    dslot *x = d->slots[s];
    const char *view;
    if (!rebind_view(d, s, &x->image.manifest, &im.manifest, &view, &err)) {
        unload_image_kernel(d, &im);
        drv_image_free(&im);
        *out = err;
        return false;
    }
    uint64_t old_hash = x->image.hash, new_hash = im.hash;
    char *dir = mesh_xstrdup(im.dir);
    /* One previous image is retained; an older one is unloaded now. */
    dimage older = x->prev;
    bool had = x->has_prev;
    x->prev = x->image;
    x->has_prev = true;
    x->image = im;
    x->loads++;
    if (had) {
        unload_image_kernel(d, &older);
        drv_image_free(&older);
    }
    mesh_info("upgrade %s (slot %u) %016llx -> %016llx from %s: view %s", drv_name(x), s, (unsigned long long)old_hash, (unsigned long long)new_hash,
              dir, view);
    free(dir);
    drv_sync_place(d, s);
    *out = mesh_buf_fmt("view %s", view);
    return true;
}

static bool op_rollback(mesh_driver *d, uint32_t s, char **out) {
    dslot *x = d->slots[s];
    if (!x) { *out = mesh_xstrdup("the program was unloaded meanwhile"); return false; }
    if (!x->has_prev) { *out = mesh_buf_fmt("%s: no previous image", drv_name(x)); return false; }
    dimage cur = x->image;
    x->image = x->prev;
    x->prev = cur;
    const char *view;
    char *err = NULL;
    if (!rebind_view(d, s, &x->prev.manifest, &x->image.manifest, &view, &err)) {
        dimage back = x->image;
        x->image = x->prev;
        x->prev = back;
        *out = err;
        return false;
    }
    const char *names[MESH_PROG_SLOTS];
    for (uint32_t i = 0; i < MESH_PROG_SLOTS; i++) names[i] = d->slots[i] ? drv_name(d->slots[i]) : NULL;
    x->loads++;
    x->reads_mask = mesh_reads_mask(&x->image.manifest, names, MESH_PROG_SLOTS);
    uint8_t e[MESH_PT_BYTES], ext[MESH_PX_BYTES];
    drv_entry(x, e);
    drv_ext(x, ext);
    mesh_pt_write_entry(d->sh->mb, s, e, ext, drv_state(x));
    mesh_info("rollback %s (slot %u) to %016llx (%s): view %s", drv_name(x), s, (unsigned long long)x->image.hash, x->image.dir, view);
    drv_sync_place(d, s);
    *out = mesh_buf_fmt("view %s", view);
    return true;
}

/* Zero the view; OP_INIT at the (next) launch. An inbox in it is held first, and zeroed only once no push into it is in flight. */
bool drv_op_reset(mesh_driver *d, uint32_t s, char **text) {
    mesh_mailbox mb = d->sh->mb;
    dslot *x = d->slots[s];
    bool inbox = x->image.manifest.inbox != NULL;
    uint32_t fed = x->fed_mask & ~(1u << s);
    if (inbox) {
        mesh_pt_set_hold(mb, s, true);
        uint32_t busy = drv_pipe_quiet(d, fed, PIPE_DEADLINE_NS);
        if (busy) {
            mesh_pt_set_hold(mb, s, false);
            *text = mesh_buf_fmt("reset %s: producers 0x%x still had a push into its inbox in flight after 500ms; nothing changed", drv_name(x), busy);
            return false;
        }
    }
    mesh_mailbox_zero(mb, x->view_off, x->image.manifest.view_bytes);
    size_t k = 0;
    for (size_t i = 0; i < d->ndead; i++) if (d->dead[i].consumer != s) d->dead[k++] = d->dead[i];
    d->ndead = k;
    if (inbox) mesh_pt_set_hold(mb, s, false);
    x->needs_init = true;
    if (x->image.manifest.peer == MESH_PEER_MODE_LOCKSTEP) drv_zero_exchange(d);
    mesh_info("reset %s (slot %u): view zeroed, OP_INIT %s", drv_name(x), s, x->want_run ? "now" : "at the next start");
    *text = mesh_xstrdup("");
    return true;
}

static bool apply(mesh_driver *d, dblink *b, char **out) {
    switch (b->k) {
    case OP_LOAD: { mesh_prepared *p = b->p; b->p = NULL; return op_load(d, p, b->run, out); }
    case OP_UPGRADE: { mesh_prepared *p = b->p; b->p = NULL; return op_upgrade(d, b->slot, p, out); }
    case OP_ROLLBACK: return op_rollback(d, b->slot, out);
    case OP_RESET: return drv_op_reset(d, b->slot, out);
    default: *out = mesh_xstrdup(""); return true;
    }
}

static void do_blink(mesh_driver *d, dblink b) {
    mesh_mailbox mb = d->sh->mb;
    int64_t drain = mesh_mono_ns() - b.since;
    bool global = !b.has_scope;
    /* mps, the CPU executors: a module change halts only its scope but frees and counts like a whole-context blink. */
    bool iso = drv_be(d, b.exec)->ops->isolated(drv_be(d, b.exec));
    bool modules = global || (iso && (b.k == OP_LOAD || b.k == OP_UPGRADE || b.k == OP_FREE));
    bool scope_mask[MESH_PROG_SLOTS], filled[MESH_PROG_SLOTS];
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) { scope_mask[s] = drv_in_scope(d, b.has_scope, b.scope, s); filled[s] = d->slots[s] != NULL; }
    int64_t t0 = d->halting ? d->halting_t : mesh_mono_ns();
    d->halting = false;
    char *load_dir = b.k == OP_LOAD && b.p ? mesh_xstrdup(mesh_prepared_dir(b.p)) : NULL;
    /* H_BLINK only now, after the drain: nodes holding no ticket take none while it is set. */
    if (global) mesh_mailbox_publish(mb, MESH_H_BLINK, 1);
    /* 1. Halt. */
    uint32_t live[MESH_PROG_SLOTS], left[MESH_PROG_SLOTS];
    size_t nlive = 0, nleft = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++)
        if (scope_mask[s] && d->slots[s] && drv_live(d->slots[s])) live[nlive++] = s;
    for (size_t i = 0; i < nlive; i++) { mesh_mailbox_publish(mb, mesh_prog_ctl(live[i]) + MESH_P_SHUTDOWN, MESH_SHUTDOWN_HALT); left[nleft++] = live[i]; }
    int64_t deadline = t0 + HALT_NS;
    char *stuck = NULL;
    while (nleft) {
        size_t k = 0;
        char *err = NULL;
        for (size_t i = 0; i < nleft; i++) {
            bool h = false;
            char *e = NULL;
            if (!drv_halted(d, left[i], &h, &e)) { if (!err) err = e; else free(e); continue; }
            if (!h) left[k++] = left[i];
        }
        nleft = k;
        if (err) { drv_context_fault(d, err); free(err); }
        if (mesh_mono_ns() > deadline && nleft) { stuck = mesh_buf_fmt("%zu program kernel(s) did not halt in 3s", nleft); break; }
    }
    for (size_t i = 0; i < nlive; i++) {
        bool still = false;
        for (size_t j = 0; j < nleft; j++) still |= left[j] == live[i];
        if (!still && d->slots[live[i]]) drv_stopped(d, live[i]);
    }
    for (size_t i = 0; i < nleft; i++) if (d->slots[left[i]]) drv_fail(d, left[i], mesh_xstrdup("did not halt in 3s (blink)"));
    int64_t t_halt = mesh_mono_ns() - t0;
    /* 2. Module operations: only when nothing of the context runs. */
    bool ok;
    char *result = NULL;
    if (stuck) {
        ok = false;
        result = mesh_buf_fmt("blink: %s; no module changed", stuck);
    } else if (d->fault) {
        ok = false;
        result = mesh_xstrdup("the CUDA context is lost; restart meshd");
    } else {
        if (modules) {
            /* A GPU module unloads only when nothing of its context runs; a CPU one, and views, at any module blink of theirs. */
            bool gpu_ok = global || d->gpu->ops->isolated(d->gpu);
            uint32_t gpu_kind = drv_gpu_kind(d);
            size_t k = 0;
            for (size_t i = 0; i < d->nzombies; i++) {
                dzombie z = d->zombies[i];
                if (z.exec == gpu_kind && !gpu_ok) { d->zombies[k++] = z; continue; }
                if (z.kernel) {
                    char *e = NULL;
                    if (!drv_be(d, z.exec)->ops->unload(drv_be(d, z.exec), z.kernel, &e)) { mesh_error("module unload: %s", e); free(e); }
                    z.kernel = NULL;
                }
                /* A dropped inbox: its pages are reused only once no producer can still be writing into it. */
                if (z.has_inbox) {
                    uint32_t busy = drv_pipe_quiet(d, z.fed, PIPE_SAMPLE_NS);
                    if (busy) {
                        if (mesh_mono_ns() - z.since > PIPE_DEADLINE_NS && !z.warned) {
                            z.warned = true;
                            mesh_buf l = MESH_BUF_INIT;
                            mesh_buf_printf(&l, "pipes: a dropped inbox's view (%zu pages at 0x%x) is kept: producers 0x%x have had pushes in flight since ",
                                            mesh_view_pages(z.view_bytes), z.view_off, busy);
                            mesh_rs_fixed(&l, ms(mesh_mono_ns() - z.since), 0);
                            mesh_buf_puts(&l, " ms");
                            mesh_error("%s", mesh_buf_str(&l));
                            mesh_buf_free(&l);
                        }
                        d->zombies[k++] = z;
                        continue;
                    }
                }
                mesh_view_heap_free(&d->heap, z.view_off, z.view_bytes);
            }
            d->nzombies = k;
        }
        ok = apply(d, &b, &result);
        if (global || iso) {
            uint32_t rebind[MESH_PROG_SLOTS];
            size_t nr = rewrite_entries(d, rebind);
            /* Only isolated executors leave kernels running across a module change. */
            for (size_t i = 0; i < nr; i++) {
                uint32_t s = rebind[i], exec = drv_exec_of(d, s);
                if (!drv_be(d, exec)->ops->isolated(drv_be(d, exec))) continue;
                dblink rb = {OP_REBIND, NULL, -1, 0, NULL, true, s, exec, mesh_mono_ns()};
                drv_push_blink(d, rb);
            }
        }
        char *e = NULL;
        if (!size_stack(d, &e)) {
            drv_context_fault(d, e);
            free(result);
            result = e;
            ok = false;
        }
    }
    free(stuck);
    int64_t t_mod = mesh_mono_ns() - t0;
    /* 3. Relaunch everything that runs (and a slot the op filled), then wait for all of it. */
    uint32_t starting[MESH_PROG_SLOTS];
    size_t nstart = 0;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS; s++) {
        dslot *x = d->slots[s];
        if (!(scope_mask[s] || !filled[s]) || !x || x->phase != PH_STOPPED || !x->want_run || x->failed || x->unloading || d->fault) continue;
        char *e = NULL;
        if (drv_launch_slot(d, s, &e)) starting[nstart++] = s;
        else {
            char *why = mesh_buf_fmt("launch: %s", e);
            free(e);
            drv_fail(d, s, why);
        }
    }
    int64_t t_launch = mesh_mono_ns();
    /* mps and the CPU executors: no waiting here either; phase_step sees them online (or fails them). */
    int64_t wait_until = t_launch + (iso ? 0 : ONLINE_NS);
    for (;;) {
        bool up = true;
        for (size_t i = 0; i < nstart; i++)
            up &= mesh_mailbox_get(mb, mesh_prog_ctl(starting[i]) + MESH_P_ONLINE) >= d->slots[starting[i]]->image.manifest.nodes;
        if (up || mesh_mono_ns() > wait_until) break;
    }
    for (size_t i = 0; i < nstart; i++) {
        uint32_t s = starting[i];
        if (!d->slots[s]) continue;
        uint32_t n = d->slots[s]->image.manifest.nodes, on = mesh_mailbox_get(mb, mesh_prog_ctl(s) + MESH_P_ONLINE);
        if (on >= n) drv_running(d, s, t_launch);
        else if (!iso) drv_fail(d, s, mesh_buf_fmt("%u of %u nodes online after 2s (blink)", on, n));
    }
    int64_t total = mesh_mono_ns() - t0;
    if (global) mesh_mailbox_publish(mb, MESH_H_BLINK, 0);
    if (modules) {
        d->epoch++;
        mesh_mailbox_publish(mb, MESH_H_EPOCH, d->epoch);
        mesh_host_block *h = &d->sh->host;
        mesh_host_block_set_u32(h, MESH_S_EPOCH, d->epoch);
        mesh_host_block_set_u32(h, MESH_S_BLINK_US, (uint32_t)(total / 1000));
        mesh_host_block_set_u64(h, MESH_S_LAST_BLINK_MS, mesh_host_block_uptime_ms(h));
        mesh_host_block_set_u32(h, MESH_S_HEAP_FREE, mesh_view_heap_free_pages(&d->heap));
        mesh_host_block_set_u32(h, MESH_S_HEAP_RUN, mesh_view_heap_largest_run(&d->heap));
    }
    long long us = total / 1000;
    mesh_buf l = MESH_BUF_INIT;
    mesh_buf_puts(&l, "blink");
    if (modules) mesh_buf_printf(&l, " epoch %u", d->epoch);
    if (!b.has_scope) mesh_buf_puts(&l, " (whole context)");
    else if (b.scope == MESH_NO_SLOT) mesh_buf_puts(&l, " (no program halted)");
    else mesh_buf_printf(&l, " (slot %u)", b.scope);
    mesh_buf_puts(&l, ": drain ");
    mesh_rs_fixed(&l, ms(drain), 2);
    mesh_buf_printf(&l, " ms, halt %lld us, modules %lld us, relaunch %lld us (%zu programs), total %lld us -> %s %s", (long long)(t_halt / 1000),
                    (long long)((t_mod - t_halt) / 1000), (long long)((total - t_mod) / 1000), nstart, us, ok ? "ok" : "err", result);
    mesh_info("%s", mesh_buf_str(&l));
    mesh_buf_free(&l);
    /* Replies: load `slot S blink U`, the others `blink U [view kept|reset]`. */
    char *reply;
    if (!ok) reply = mesh_xstrdup(result);
    else if (!*result) reply = mesh_buf_fmt("blink %lld", us);
    else if (strncmp(result, "slot ", 5) == 0) reply = mesh_buf_fmt("%s blink %lld", result, us);
    else reply = mesh_buf_fmt("blink %lld %s", us, result);
    d->state_dirty = true;
    if (load_dir && !ok) {
        char *e = mesh_buf_fmt("%s: %s", load_dir, reply);
        drv_startup_failed(d, b.reply, e);
        free(e);
    }
    drv_answer(b.reply, ok, reply);
    free(reply);
    free(result);
    free(load_dir);
    if (b.p) { mesh_prepared_free(b.p); free(b.p); }
}

void drv_blink_tick(mesh_driver *d) {
    if (!d->nblinks) return;
    dblink *b = &d->blinks[0];
    if (d->fault) {
        dblink x = *b;
        memmove(d->blinks, d->blinks + 1, (d->nblinks - 1) * sizeof *d->blinks);
        d->nblinks--;
        mesh_reply_err(x.reply, "the CUDA context is lost; restart meshd");
        if (x.p) { mesh_prepared_free(x.p); free(x.p); }
        return;
    }
    bool iso = drv_be(d, b->exec)->ops->isolated(drv_be(d, b->exec));
    bool drained = true;
    for (uint32_t s = 0; s < MESH_PROG_SLOTS && drained; s++)
        if (drv_in_scope(d, b->has_scope, b->scope, s) && d->slots[s] && !mesh_pring_idle(&d->slots[s]->ring)) drained = false;
    if (drained) {
        if (iso && !halt_scope(d, b->has_scope, b->scope)) return;
        dblink x = *b;
        memmove(d->blinks, d->blinks + 1, (d->nblinks - 1) * sizeof *d->blinks);
        d->nblinks--;
        do_blink(d, x);
    } else if (mesh_mono_ns() - b->since > DRAIN_NS) {
        dblink x = *b;
        memmove(d->blinks, d->blinks + 1, (d->nblinks - 1) * sizeof *d->blinks);
        d->nblinks--;
        const char *e = "blink: in-flight tasks did not drain in 3s; nothing changed";
        mesh_error("%s", e);
        mesh_reply_err(x.reply, e);
        if (x.p) { mesh_prepared_free(x.p); free(x.p); }
    }
}

/* Keeps slot_free referenced for the unload paths that free a whole slot here. */
void drv_slot_free(dslot *x);
void drv_slot_free(dslot *x) { slot_free(x); }
