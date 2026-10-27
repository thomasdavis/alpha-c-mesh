/* The reconciler's ops (reconcile.rs's Rec methods that touch the driver, the catalog and the fleet
 * thread). */
#include "fleetd_int.h"

#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/loader.h"
#include "mesh/log.h"
#include "mesh/rsfmt.h"

#include <stdlib.h>
#include <string.h>

#define F(c) ((mesh_fleetd *)(c))
#define SH(c) (F(c)->sh)

static mfl_actual *inventory(void *c, size_t *n) { return fd_inventory(SH(c), 10000, n); }

/* An image of name at hash: the local catalog's when it is that image, else a member's. */
static bool source(void *c, const char *name, const char *dir, uint64_t hash, void **src, char **err) {
    mesh_fleetd *f = c;
    mesh_shared *sh = f->sh;
    mesh_payload p;
    mesh_manifest m;
    uint64_t h;
    char *e = NULL;
    if (mesh_loader_identity(sh->programs, dir, &p, &m, &h, &e)) {
        bool same = h == hash && strcmp(m.name, name) == 0;
        mesh_manifest_free(&m);
        if (same) {
            mesh_src *s = mesh_xmalloc(sizeof *s);
            *s = mesh_src_bytes(&p, hash);
            mesh_payload_free(&p);
            *src = s;
            return true;
        }
        mesh_payload_free(&p);
    } else free(e);
    mpr_relay *r = fd_relay(f);
    if (!r) {
        *err = mesh_xstrdup("fleet thread gone");
        return false;
    }
    mpr_payload got;
    e = NULL;
    if (!mpr_fetch_image(r, name, hash, dir, FD_FETCH_MS, &got, &e)) {
        *err = mesh_buf_fmt("image %s %016llx: %s", name, (unsigned long long)hash, e ? e : "");
        free(e);
        return false;
    }
    mesh_info("reconcile: %s %016llx: image from a member (%zu bytes of PTX)", name, (unsigned long long)hash, got.ptx.n);
    mesh_payload q = {got.dir, got.manifest, got.ptx.b, got.ptx.n};
    mesh_src *s = mesh_xmalloc(sizeof *s);
    *s = mesh_src_bytes(&q, hash);
    mpr_payload_free(&got);
    *src = s;
    return true;
}

static void src_free(void *c, void *src) {
    (void)c;
    mesh_src_free(src);
    free(src);
}

/* sh.ask(.., APPLY): the driver's Ok text, or its error. */
static bool ask(mesh_shared *sh, mesh_cmd cmd, int64_t ms, char **text, char **err) {
    char *t = NULL;
    if (mesh_shared_ask(sh, cmd, ms, &t)) {
        *text = t;
        return true;
    }
    *err = t;
    return false;
}

static mesh_cmd named(mesh_cmd_kind k, const char *name) {
    mesh_cmd c = mesh_cmd_new(k);
    c.name = mesh_xstrdup(name);
    return c;
}

static bool load(void *c, void *src, bool run, char **text, char **err) {
    mesh_cmd x = mesh_cmd_new(MESH_CMD_LOAD);
    x.has_src = true;
    x.src = mesh_src_copy(src);
    x.run = run;
    return ask(SH(c), x, FD_APPLY_MS, text, err);
}

static bool upgrade(void *c, const char *name, void *src, char **text, char **err) {
    mesh_cmd x = named(MESH_CMD_UPGRADE, name);
    x.has_src = true;
    x.src = mesh_src_copy(src);
    return ask(SH(c), x, FD_APPLY_MS, text, err);
}

static bool unload(void *c, const char *name, char **text, char **err) {
    mesh_cmd x = named(MESH_CMD_UNLOAD, name);
    x.now = false;
    return ask(SH(c), x, FD_APPLY_MS, text, err);
}

static bool rollback(void *c, const char *name, char **text, char **err) { return ask(SH(c), named(MESH_CMD_ROLLBACK, name), FD_APPLY_MS, text, err); }

static bool prog_run(void *c, const char *name, bool on, char **text, char **err) {
    mesh_cmd x = named(MESH_CMD_PROG_RUN, name);
    x.on = on;
    return ask(SH(c), x, FD_APPLY_MS, text, err);
}

static bool reset(void *c, const char *name, char **text, char **err) { return ask(SH(c), named(MESH_CMD_RESET, name), FD_APPLY_MS, text, err); }

/* A lockstep cycle's task: its result, or "no result in 60s". */
static bool task(void *c, const char *name, uint32_t op, uint32_t arg, uint32_t *result, char **err) {
    mesh_cmd x = named(MESH_CMD_TASK, name);
    x.op = op;
    x.arg = arg;
    mesh_result res;
    char *e = NULL;
    if (!fd_ask(SH(c), x, FD_APPLY_MS, &res, &e)) {
        if (strncmp(e, "no answer from the driver in ", 29) == 0) {
            free(e);
            mesh_buf b = MESH_BUF_INIT;
            mesh_buf_puts(&b, "no result in ");
            mesh_rs_duration_ms(&b, FD_APPLY_MS);
            e = mesh_buf_take(&b);
        }
        *err = e;
        return false;
    }
    *result = res.value;
    mesh_result_free(&res);
    return true;
}

static bool place(void *c, const char *name, const mt_strs *words, char **text, char **err) {
    mesh_cmd x = named(MESH_CMD_PLACE, name);
    x.has_words = true;
    x.nwords = words->n;
    x.words = mesh_xcalloc(words->n + 1, sizeof *x.words);
    for (size_t i = 0; i < words->n; i++) x.words[i] = mesh_xstrdup(words->v[i]);
    return ask(SH(c), x, FD_PLACE_MS, text, err);
}

static bool fetch_view(void *c, const char *name, uint64_t hash, uint32_t from, uint32_t *view_abi, mpw_bytes *bytes, char **err) {
    mpr_relay *r = fd_relay(F(c));
    if (!r) {
        *err = mesh_xstrdup("fleet thread gone");
        return false;
    }
    return mpr_fetch_view(r, name, hash, from, FD_FETCH_MS, view_abi, bytes, err);
}

static bool view_put(void *c, const char *name, uint32_t view_abi, const mpw_bytes *bytes, char **text, char **err) {
    mesh_cmd x = named(MESH_CMD_VIEW_PUT, name);
    x.view_abi = view_abi;
    x.len = bytes->n;
    x.bytes = mesh_xmalloc(bytes->n ? bytes->n : 1);
    if (bytes->n) memcpy(x.bytes, bytes->b, bytes->n);
    return ask(SH(c), x, FD_APPLY_MS, text, err);
}

/* Stop feeding the lockstep program and wait (2 s at most) until the fleet thread saw it. */
static void quiesce(void *c, bool mirror, bool member) {
    mesh_fleetd *f = c;
    atomic_store(&f->sh->lock_feed, false);
    pthread_mutex_lock(&f->mu);
    f->want_set = false;
    f->mirror = mirror;
    f->member = member;
    pthread_mutex_unlock(&f->mu);
    mpr_relay *r = fd_relay(f);
    if (!r) return;
    mpr_group_want(r, NULL, mirror, member);
    int64_t t0 = mesh_mono_ms();
    for (;;) {
        uint32_t s = mpr_group_state(r);
        if ((s != MESH_GL_RUNNING && s != MESH_GL_HELLO) || mesh_mono_ms() - t0 >= 2000) break;
        mesh_sleep_us(2000);
    }
}

static bool want_set(void *c) {
    mesh_fleetd *f = c;
    pthread_mutex_lock(&f->mu);
    bool w = f->want_set;
    pthread_mutex_unlock(&f->mu);
    return w;
}

static void group_words(void *c, uint32_t rank, uint32_t n, uint32_t epoch) {
    mesh_mailbox mb = SH(c)->mb;
    mesh_mailbox_set(mb, MESH_H_GROUP_RANK, rank);
    mesh_mailbox_set(mb, MESH_H_GROUP_EPOCH, epoch);
    mesh_mailbox_set(mb, MESH_H_GROUP_N, n);
}

static void ready(void *c, const mpk_hello *spec) {
    mesh_fleetd *f = c;
    pthread_mutex_lock(&f->mu);
    f->want_set = true;
    bool mirror = f->mirror, member = f->member;
    pthread_mutex_unlock(&f->mu);
    mpr_relay *r = fd_relay(f);
    if (r) mpr_group_want(r, spec, mirror, member);
}

static bool is_leader(void *c) {
    mesh_shared *sh = SH(c);
    return mesh_host_block_get_u32(&sh->host, MESH_S_LEADER) == sh->me;
}

static void status(void *c, uint64_t applied, uint32_t st, const char *reason, const mst_outcome *moves, size_t nmoves) {
    mesh_fleetd *f = c;
    pthread_mutex_lock(&f->mu);
    f->applied = applied;
    f->status = st;
    free(f->reason);
    f->reason = mesh_xstrdup(reason);
    for (size_t i = 0; i < f->nmoves; i++) free(f->moves[i].name);
    free(f->moves);
    f->moves = nmoves ? mesh_xcalloc(nmoves, sizeof *f->moves) : NULL;
    for (size_t i = 0; i < nmoves; i++) {
        f->moves[i] = moves[i];
        f->moves[i].name = mesh_xstrdup(moves[i].name);
    }
    f->nmoves = nmoves;
    pthread_mutex_unlock(&f->mu);
    mpr_relay *r = atomic_load(&f->relay);
    if (r) mpr_wake(r);
}

static void fails(void *c, char *const *names, const uint64_t *hashes, const bool *unsupported, char *const *whys, size_t n) {
    mesh_fleetd *f = c;
    msr_fail *v = n ? mesh_xcalloc(n, sizeof *v) : NULL;
    for (size_t i = 0; i < n; i++) v[i] = (msr_fail){mesh_xstrdup(names[i]), hashes[i], unsupported[i], mesh_xstrdup(whys[i])};
    pthread_mutex_lock(&f->mu);
    for (size_t i = 0; i < f->nfails; i++) {
        free(f->fails[i].name);
        free(f->fails[i].why);
    }
    free(f->fails);
    f->fails = v;
    f->nfails = n;
    pthread_mutex_unlock(&f->mu);
}

static uint64_t applied(void *c) {
    mesh_fleetd *f = c;
    pthread_mutex_lock(&f->mu);
    uint64_t a = f->applied;
    pthread_mutex_unlock(&f->mu);
    return a;
}

static void logline(void *c, bool error, const char *line) {
    (void)c;
    if (error) mesh_error("%s", line);
    else mesh_info("%s", line);
}

static void sleep_ms(void *c, uint32_t ms) {
    (void)c;
    mesh_sleep_us((int64_t)ms * 1000);
}

mrc_ops fd_recon_ops(mesh_fleetd *f) {
    return (mrc_ops){
        .ctx = f,
        .me = f->sh->me,
        .inventory = inventory,
        .source = source,
        .src_free = src_free,
        .load = load,
        .upgrade = upgrade,
        .unload = unload,
        .rollback = rollback,
        .prog_run = prog_run,
        .reset = reset,
        .task = task,
        .place = place,
        .fetch_view = fetch_view,
        .view_put = view_put,
        .quiesce = quiesce,
        .want_set = want_set,
        .group_words = group_words,
        .ready = ready,
        .is_leader = is_leader,
        .status = status,
        .fails = fails,
        .applied = applied,
        .log = logline,
        .sleep_ms = sleep_ms,
    };
}
