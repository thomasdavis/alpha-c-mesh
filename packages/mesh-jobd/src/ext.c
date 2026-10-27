/* mesh-jobd: opening (the job log replayed, the blob store indexed), the threads' lifetime, and what
 * the other threads call: the fleet's view and scheduler, frame routing, `render`. */
#include "internal.h"

#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/log.h"
#include "mesh/strbuf.h"

#include <stdlib.h>
#include <string.h>

/* ---- the blob thread's events */

void mjd_bev_free(void *p) {
    mjd_bev *e = p;
    free(e->bytes);
    if (e->news) {
        mbd_news_free(e->news);
        free(e->news);
    }
    for (size_t i = 0; i < e->nwords; i++) free(e->words[i]);
    free(e->words);
    mesh_reply_drop(e->reply);
    free(e->err);
    memset(e, 0, sizeof *e);
}

void mjd_blobs_send(mesh_shared *sh, mjd_bev *e) {
    if (!sh->blobs || mesh_chan_send(sh->blobs, e) != MESH_CHAN_OK) mjd_bev_free(e);
}

/* ---- sh->jobs_ext */

mj_view mjd_ext_view(mesh_shared *sh) {
    mjd_ext *x = mjd_ext_of(sh);
    mj_view v = {0};
    if (!x) return v;
    pthread_mutex_lock(&x->mu);
    v = x->view;
    pthread_mutex_unlock(&x->mu);
    return v;
}

size_t mjd_ext_watch(mesh_shared *sh, mj_job_id **out) {
    mjd_ext *x = mjd_ext_of(sh);
    *out = NULL;
    if (!x) return 0;
    pthread_mutex_lock(&x->mu);
    size_t n = x->nwatch;
    if (n) {
        *out = mxalloc(n * sizeof **out);
        memcpy(*out, x->watch, n * sizeof **out);
    }
    pthread_mutex_unlock(&x->mu);
    return n;
}

/* The set is kept in id order (Rust's BTreeSet). */
void mjd_ext_set_watch(mesh_shared *sh, mj_job_id id, bool on) {
    mjd_ext *x = mjd_ext_of(sh);
    if (!x) return;
    pthread_mutex_lock(&x->mu);
    size_t i = 0;
    while (i < x->nwatch && x->watch[i] < id) i++;
    bool has = i < x->nwatch && x->watch[i] == id;
    if (on && !has) {
        if (x->nwatch == x->capwatch) {
            x->capwatch = x->capwatch ? 2 * x->capwatch : 8;
            x->watch = mxrealloc(x->watch, x->capwatch * sizeof *x->watch);
        }
        memmove(&x->watch[i + 1], &x->watch[i], (x->nwatch - i) * sizeof *x->watch);
        x->watch[i] = id;
        x->nwatch++;
    } else if (!on && has) {
        memmove(&x->watch[i], &x->watch[i + 1], (x->nwatch - i - 1) * sizeof *x->watch);
        x->nwatch--;
    }
    pthread_mutex_unlock(&x->mu);
}

void mjd_set_view(mesh_shared *sh, mj_view v) {
    mjd_ext *x = mjd_ext_of(sh);
    if (!x) return;
    pthread_mutex_lock(&x->mu);
    x->view = v;
    pthread_mutex_unlock(&x->mu);
}

void mjd_set_sched(mesh_shared *sh, mj_sched *cx) {
    mjd_ext *x = mjd_ext_of(sh);
    if (!x) {
        if (cx && cx->release) cx->release(cx->ctx);
        free(cx);
        return;
    }
    pthread_mutex_lock(&x->mu);
    mj_sched *old = x->sched;
    x->sched = cx;
    pthread_mutex_unlock(&x->mu);
    if (old) {
        if (old->release) old->release(old->ctx);
        free(old);
    }
}

/* ---- frames and render */

bool mjd_route_frame(mesh_shared *sh, uint32_t from, const uint8_t *frame, size_t n) {
    if (n < 1 + MESH_JOB_HEAD) return false;
    uint8_t ty = frame[0];
    uint32_t len = (uint32_t)frame[1] | (uint32_t)frame[2] << 8 | (uint32_t)frame[3] << 16 | (uint32_t)frame[4] << 24;
    if ((size_t)len != n - 1 - MESH_JOB_HEAD) return false;
    const uint8_t *body = frame + 1 + MESH_JOB_HEAD;
    if (ty >= MESH_F_JOB_AVAIL && ty <= MESH_F_RESULT) {
        mesh_job_event e = mesh_job_event_new(MESH_JEV_FRAME);
        e.from = from;
        e.ty = ty;
        e.len = len;
        e.body = mxalloc(len ? len : 1);
        memcpy(e.body, body, len);
        if (mesh_chan_send(sh->jobs, &e) != MESH_CHAN_OK) {
            mesh_job_event_free(&e);
            return false;
        }
        return true;
    }
    if (ty >= MESH_F_BLOB_PUT && ty <= MESH_F_BLOB) {
        if (!sh->blobs) return false;
        mjd_bev e = {.k = MJD_BEV_FRAME, .from = from, .ty = ty, .n = len};
        e.bytes = mxalloc(len ? len : 1);
        memcpy(e.bytes, body, len);
        if (mesh_chan_send(sh->blobs, &e) != MESH_CHAN_OK) {
            mjd_bev_free(&e);
            return false;
        }
        return true;
    }
    return false;
}

bool mjd_render(mesh_shared *sh, char **words, size_t n, uint8_t **out, size_t *len, char **err) {
    *out = NULL;
    *len = 0;
    *err = NULL;
    if (!sh->blobs) {
        *err = mstrdup("the blob thread is gone");
        return false;
    }
    mesh_reply *r = mesh_reply_new();
    mjd_bev e = {.k = MJD_BEV_RENDER, .nwords = n, .reply = mesh_reply_sender(r)};
    e.words = mxalloc((n ? n : 1) * sizeof *e.words);
    for (size_t i = 0; i < n; i++) e.words[i] = mstrdup(words[i]);
    if (mesh_chan_send(sh->blobs, &e) != MESH_CHAN_OK) {
        mjd_bev_free(&e);
        mesh_reply_release(r);
        *err = mstrdup("the blob thread is gone");
        return false;
    }
    mesh_result x;
    int w = mesh_reply_wait(r, 10000, &x);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) {
        *err = mstrdup(w == MESH_CHAN_CLOSED ? "the blob thread is gone" : "render: no answer from the blob thread in 10 s");
        return false;
    }
    bool ok = x.ok;
    if (ok) {
        *out = x.bytes;
        *len = x.len;
        x.bytes = NULL;
    } else {
        *err = x.text ? x.text : mstrdup("");
        x.text = NULL;
    }
    mesh_result_free(&x);
    return ok;
}

/* ---- lifetime */

static void store_log(void *ctx, const char *line) {
    (void)ctx;
    mesh_info("%s", line);
}
static void blob_log(void *ctx, const char *line) {
    (void)ctx;
    mesh_error("%s", line);
}
static void drop_write(void *p) { mbd_write_free(p); }

mjd *mjd_open(mesh_shared *sh, const mjd_config *cfg, char **err) {
    mjd *d = calloc(1, sizeof *d);
    if (!d) abort();
    d->sh = sh;
    d->store = mjd_store_open(cfg->jobs_state, &d->rep, store_log, NULL, err);
    if (!d->store) {
        mj_replica_free(&d->rep);
        free(d);
        return NULL;
    }
    d->bstore = mb_open(cfg->blobs, cfg->blobs_cap, err);
    if (!d->bstore) {
        mjd_store_free(d->store);
        mj_replica_free(&d->rep);
        free(d);
        return NULL;
    }
    mb_set_log(d->bstore, blob_log, NULL);
    /* Its own leader when single; in a fleet the job table follows the fleet's leader (a majority commits). */
    bool peered = sh->peer != NULL;
    uint32_t n = sh->n ? sh->n : 1;
    d->cfg = (mj_cfg){peered ? sh->me : 0, n, (!peered || n == 2) ? 1 : n / 2 + 1, cfg->dead_ms};
    d->ext = calloc(1, sizeof *d->ext);
    if (!d->ext) abort();
    pthread_mutex_init(&d->ext->mu, NULL);
    sh->jobs_ext = d->ext;
    sh->blobs = mesh_chan_new(sizeof(mjd_bev), 0, mjd_bev_free);
    d->wq = mesh_chan_new(sizeof(mbd_write), 0, drop_write);
    return d;
}

static void spawn(pthread_t *t, void *(*f)(void *), void *arg) {
    if (pthread_create(t, NULL, f, arg) != 0) {
        mesh_error("jobs: cannot start a thread");
        abort();
    }
}

void mjd_start(mjd *d) {
    if (d->started) return;
    d->started = true;
    for (unsigned i = 0; i < MESH_BLOBD_WRITERS; i++) spawn(&d->writers[i], mjd_writer_main, d);
    spawn(&d->blobs_t, mjd_blobs_main, d);
    spawn(&d->jobs_t, mjd_jobs_main, d);
}

void mjd_stop(mjd *d) {
    if (!d->started) return;
    atomic_store(&d->stop, true);
    pthread_join(d->jobs_t, NULL);
    pthread_join(d->blobs_t, NULL); /* its writer queue's sender goes with it: the writers drain and end */
    for (unsigned i = 0; i < MESH_BLOBD_WRITERS; i++) pthread_join(d->writers[i], NULL);
    d->started = false;
}

void mjd_free(mjd *d) {
    if (!d) return;
    mjd_stop(d);
    mjd_store_free(d->store);
    mj_replica_free(&d->rep);
    mb_store_free(d->bstore);
    mesh_chan_release(d->wq);
    if (d->sh->jobs_ext == d->ext) d->sh->jobs_ext = NULL;
    mesh_chan *b = d->sh->blobs;
    d->sh->blobs = NULL;
    if (b) mesh_chan_release(b);
    if (d->ext->sched) {
        if (d->ext->sched->release) d->ext->sched->release(d->ext->sched->ctx);
        free(d->ext->sched);
    }
    free(d->ext->watch);
    pthread_mutex_destroy(&d->ext->mu);
    free(d->ext);
    free(d);
}
