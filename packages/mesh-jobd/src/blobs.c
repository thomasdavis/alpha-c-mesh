/* The blob thread (blobd.rs `run`) and its writers (blobd.rs `writer`): @ajax/mesh-blobd over sh->blobs,
 * the peer links and the jobs thread. */
#include "internal.h"

#include "mesh/log.h"
#include "mesh/strbuf.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    mjd *d;
    mesh_shared *sh;
} bx;

static bool io_send_peer(void *c, uint32_t to, const uint8_t *f, size_t n) { return mesh_shared_send_peer(((bx *)c)->sh, to, f, n); }
static bool io_send_bulk(void *c, uint32_t to, const uint8_t *f, size_t n) { return mesh_shared_send_bulk(((bx *)c)->sh, to, f, n); }

static void refs_free(void *p) {
    mb_refs_free(p);
    free(p);
}

static void jobs_send(mesh_shared *sh, mesh_job_event *e) {
    if (mesh_chan_send(sh->jobs, e) != MESH_CHAN_OK) mesh_job_event_free(e);
}

static void refs_event(mesh_shared *sh, mesh_job_event_kind k, const mb_ref *refs, size_t n) {
    if (!n) return;
    mb_refs *v = mxalloc(sizeof *v);
    v->v = mxalloc(n * sizeof *v->v);
    memcpy(v->v, refs, n * sizeof *v->v);
    v->n = n;
    mesh_job_event e = mesh_job_event_new(k);
    e.msg = v;
    e.msg_free = refs_free;
    jobs_send(sh, &e);
}

static void io_stored(void *c, const mb_ref *refs, size_t n) { refs_event(((bx *)c)->sh, MESH_JEV_BLOBS_STORED, refs, n); }
static void io_gone(void *c, const mb_ref *refs, size_t n) { refs_event(((bx *)c)->sh, MESH_JEV_BLOB_GONE, refs, n); }

static void io_acked(void *c, uint32_t from, mj_job_id job, const mj_hash *h, bool ok) {
    mb_ref *r = mxalloc(sizeof *r);
    *r = (mb_ref){job, *h};
    mesh_job_event e = mesh_job_event_new(MESH_JEV_BLOB_ACKED);
    e.from = from;
    e.ok = ok;
    e.msg = r;
    e.msg_free = free;
    jobs_send(((bx *)c)->sh, &e);
}

static uint32_t io_alive(void *c) {
    bx *x = c;
    return mjd_ext_view(x->sh).alive | 1u << x->sh->me;
}

static bool io_leading(void *c) {
    bx *x = c;
    mj_view v = mjd_ext_view(x->sh);
    return (v.has_leader && v.leader == x->sh->me) || !x->sh->peer;
}

static void io_watching(void *c, mj_job_id job, bool on) { mjd_ext_set_watch(((bx *)c)->sh, job, on); }

static void io_mem(void *c, int64_t delta) {
    mesh_shared *sh = ((bx *)c)->sh;
    if (delta >= 0) atomic_fetch_add_explicit(&sh->blob_mem, (uint64_t)delta, memory_order_relaxed);
    else atomic_fetch_sub_explicit(&sh->blob_mem, (uint64_t)-delta, memory_order_relaxed);
}

static void io_write(void *c, mbd_write w) {
    if (mesh_chan_send(((bx *)c)->d->wq, &w) != MESH_CHAN_OK) mbd_write_free(&w);
}

static int64_t io_now(void *c) {
    (void)c;
    return mesh_mono_ms();
}

static void io_log(void *c, bool error, const char *line) {
    (void)c;
    if (error) mesh_error("%s", line);
    else mesh_info("%s", line);
}

static void event(mbd *b, mjd_bev *e) {
    switch (e->k) {
    case MJD_BEV_LOCAL:
        mbd_local(b, e->job, e->hash, e->bytes, e->n);
        e->bytes = NULL;
        break;
    case MJD_BEV_PUSH: mbd_push(b, e->to, e->job, e->hash); break;
    case MJD_BEV_FETCH: mbd_fetch(b, e->job, e->hash, e->holders); break;
    case MJD_BEV_DONE: mbd_done(b, e->job, e->hash); break;
    case MJD_BEV_FRAME: mbd_frame(b, e->from, e->ty, e->bytes, e->n); break;
    case MJD_BEV_TABLE:
        mbd_table(b, e->news);
        free(e->news);
        e->news = NULL;
        break;
    case MJD_BEV_RENDER: {
        uint8_t *out = NULL;
        size_t len = 0;
        char *err = NULL;
        if (mbd_render(b, (const char *const *)e->words, e->nwords, &out, &len, &err)) mesh_reply_bytes(e->reply, out ? out : (uint8_t *)"", len);
        else mesh_reply_err(e->reply, err ? err : "");
        e->reply = NULL;
        free(out);
        free(err);
        break;
    }
    case MJD_BEV_WRITTEN: mbd_written(b, e->job, e->hash, e->why, e->from, e->ok, e->err); break;
    }
}

void *mjd_blobs_main(void *arg) {
    mjd *d = arg;
    mesh_shared *sh = d->sh;
    bx cx = {d, sh};
    mb_store *s = d->bstore;
    d->bstore = NULL;
    mbd_io io = {&cx, io_send_peer, io_send_bulk, io_stored, io_acked, io_gone, io_alive, io_leading, io_watching, io_mem, io_write, io_now, io_log};
    /* It logs what the store holds, and the jobs thread learns it (the leader commits only what it holds, for that job). */
    mbd *b = mbd_new(d->cfg.me, s, io);
    for (;;) {
        if (atomic_load(&sh->fatal) || atomic_load(&d->stop)) break;
        mjd_bev e;
        int w = mesh_chan_recv(sh->blobs, &e, 5);
        if (w == MESH_CHAN_CLOSED) break;
        if (w == MESH_CHAN_OK) {
            size_t k = 0;
            do {
                event(b, &e);
                mjd_bev_free(&e);
            } while (++k < 4096 && mesh_chan_try_recv(sh->blobs, &e) == MESH_CHAN_OK);
        }
        mbd_tick(b);
        /* References whose file turned out missing or corrupt: jobd no longer counts them. */
        mbd_report_lost(b);
    }
    mbd_free(b);
    mesh_chan_drop_sender(d->wq); /* the writers drain their queue and end */
    return NULL;
}

/* A writer: a batch of references made durable (their directories fsynced once), then each reported. */
static pthread_mutex_t writers_mu = PTHREAD_MUTEX_INITIALIZER;

void *mjd_writer_main(void *arg) {
    mjd *d = arg;
    enum { BATCH = 64 };
    mbd_write ws[BATCH];
    bool ok[BATCH];
    char *err[BATCH];
    for (;;) {
        size_t n = 0;
        pthread_mutex_lock(&writers_mu);
        int w = mesh_chan_recv(d->wq, &ws[0], -1);
        if (w == MESH_CHAN_OK) {
            n = 1;
            while (n < BATCH && mesh_chan_try_recv(d->wq, &ws[n]) == MESH_CHAN_OK) n++;
        }
        pthread_mutex_unlock(&writers_mu);
        if (w != MESH_CHAN_OK) return NULL;
        mbd_write_batch(ws, n, ok, err);
        for (size_t i = 0; i < n; i++) {
            mjd_bev e = {.k = MJD_BEV_WRITTEN, .job = ws[i].job, .hash = ws[i].hash, .why = ws[i].why, .from = ws[i].from, .ok = ok[i], .err = err[i]};
            mjd_blobs_send(d->sh, &e);
            mbd_write_free(&ws[i]);
        }
    }
}
