/* The jobs thread (jobd.rs `run`): the job node over the peer links, the driver, the job log and the
 * blob thread. */
#include "jobs_thread.h"

#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/debug_str.h"
#include "mesh/jobd/text.h"
#include "mesh/jobs/req.h"
#include "mesh/log.h"
#include "mesh/ref/sha256d.h"
#include "mesh/rs_parse.h"
#include "mesh/strbuf.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Who waits for an answer (shared.rs JobReply). */
typedef struct {
    mesh_job_reply k;
    mesh_reply *r;          /* LOCAL */
    uint32_t from, id;      /* REMOTE: the member and its request id */
} jd_reply;

static void jd_reply_free(void *p) { mesh_reply_drop(((jd_reply *)p)->r); }

uint64_t jd_unix_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static uint64_t jd_now(const jd *x) { return (uint64_t)(mesh_mono_ms() - x->t0) + 1; }

mj_view jd_view(const jd *x) {
    if (x->peered) return mjd_ext_view(x->sh);
    uint32_t me = x->d->cfg.me;
    return (mj_view){1, true, me, 1u << me, 1u << me};
}

static void answer(jd *x, jd_reply *to, bool ok, const char *text) {
    switch (to->k) {
    case MESH_JOB_REPLY_LOCAL:
        if (ok) mesh_reply_ok(to->r, text);
        else mesh_reply_err(to->r, text);
        to->r = NULL;
        break;
    case MESH_JOB_REPLY_REMOTE: {
        size_t n = strlen(text);
        if (n > MESH_F_CTL_REPLY_MAX) n = MESH_F_CTL_REPLY_MAX;
        msb f = {0};
        msb_putc(&f, MESH_F_CTL_REPLY);
        uint32_t v[3] = {to->id, ok ? 1u : 0u, (uint32_t)n};
        for (int i = 0; i < 3; i++)
            for (int b = 0; b < 4; b++) msb_putc(&f, (char)(v[i] >> (8 * b)));
        msb_put(&f, text, n);
        mesh_shared_send_peer(x->sh, to->from, (const uint8_t *)f.p, f.n);
        msb_free(&f);
        break;
    }
    case MESH_JOB_REPLY_NONE: break;
    }
}

static void blobs_send(jd *x, mjd_bev e) { mjd_blobs_send(x->sh, &e); }

static void send_cmd(jd *x, mesh_cmd *c) {
    if (mesh_chan_send(x->sh->cmd, c) != MESH_CHAN_OK) mesh_cmd_free(c);
}

/* The node's outputs, in order: a persist is synced before any send or answer after it (mj_durable);
 * one that fails stops this member (fail-stop). */
static void apply(jd *x, mj_outs *out) {
    mesh_shared *sh = x->sh;
    if (atomic_load(&sh->fatal)) {
        mj_outs_free(out);
        return;
    }
    bool ok;
    char *err = NULL;
    mj_outs outs = mj_durable(out, &x->disk, &ok, &err);
    if (!ok) {
        char *why = mfmt("jobs: cannot persist the job log (%s)", err ? err : "");
        mesh_shared_fail_stop(sh, why);
        free(why);
        free(err);
        mj_outs_free(&outs);
        return;
    }
    /* Members whose queue stayed full in this batch: the rest of their table frames are dropped at once. */
    uint32_t stalled = 0;
    for (size_t i = 0; i < outs.n; i++) {
        mj_out *o = &outs.v[i];
        switch (o->kind) {
        case MJ_OUT_PERSIST: break;
        case MJ_OUT_SEND: {
            msb f = {0};
            mj_frame_encode(&o->frame, &f);
            if (o->frame.ty == MESH_F_JOB_STATE || o->frame.ty == MESH_F_JOB_HAVE) {
                /* The replicated table (deltas, snapshots and merges in parts) and pull answers wait for room
                 * in the link's queue (a while): a burst of them larger than the queue would otherwise never
                 * arrive whole. */
                if (o->to < 32 && !(stalled >> o->to & 1) &&
                    !mesh_shared_send_peer_wait(sh, o->to, (const uint8_t *)f.p, f.n, MESH_JOBD_TABLE_SEND_WAIT_MS))
                    stalled |= 1u << o->to;
            } else {
                /* A full queue drops the frame (a lease expires, a result is sent again, the availability every second). */
                mesh_shared_send_peer(sh, o->to, (const uint8_t *)f.p, f.n);
            }
            msb_free(&f);
            break;
        }
        case MJ_OUT_RUN: {
            mesh_cmd c = mesh_cmd_new(MESH_CMD_JOB_LEASE);
            c.lease = o->run;
            memset(&o->run, 0, sizeof o->run);
            send_cmd(x, &c);
            break;
        }
        case MJ_OUT_PUSH_BLOB: blobs_send(x, (mjd_bev){.k = MJD_BEV_PUSH, .to = o->to, .job = o->job, .hash = o->hash}); break;
        case MJ_OUT_FETCH_BLOB: blobs_send(x, (mjd_bev){.k = MJD_BEV_FETCH, .job = o->job, .hash = o->hash, .holders = o->holders}); break;
        case MJ_OUT_BLOB_DONE: blobs_send(x, (mjd_bev){.k = MJD_BEV_DONE, .job = o->job, .hash = o->hash}); break;
        case MJ_OUT_CUT: {
            mesh_cmd c = mesh_cmd_new(MESH_CMD_JOB_CUT);
            c.lease_id = o->lease;
            c.cut = o->cut;
            send_cmd(x, &c);
            break;
        }
        case MJ_OUT_ANSWER: {
            jd_reply r;
            if (momap_take(x->replies, &o->token, &r)) {
                answer(x, &r, o->ok, o->text ? o->text : "");
                jd_reply_free(&r);
            }
            break;
        }
        case MJ_OUT_LOG: mesh_info("%s", o->text ? o->text : ""); break;
        }
    }
    mj_outs_free(&outs);
}

/* ---- control: `jobs`, `job status|results|submit|cancel|forget ..` */

static char *bad_id(const char *id) {
    char *q = mdebug(id);
    char *e = mfmt("%s: a job id is <term>.<n>", q);
    free(q);
    return e;
}

static bool number(char **rest, size_t nrest, size_t i, uint64_t d, uint64_t *out, char **err) {
    if (i >= nrest) {
        *out = d;
        return true;
    }
    if (rs_u64(rest[i], strlen(rest[i]), out)) return true;
    char *q = mdebug(rest[i]);
    *err = mfmt("%s: a number", q);
    free(q);
    return false;
}

static size_t split(char *s, char ***out) {
    size_t n = 0, cap = 8;
    char **w = mxalloc(cap * sizeof *w);
    for (char *p = s; *p;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') p++;
        if (!*p) break;
        char *b = p;
        while (*p && !(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')) p++;
        if (*p) *p++ = 0;
        if (n == cap) w = mxrealloc(w, (cap *= 2) * sizeof *w);
        w[n++] = b;
    }
    *out = w;
    return n;
}

static void ctl(jd *x, const char *line, jd_reply *reply, uint64_t now, uint64_t unix_ms, mj_outs *outs) {
    char *copy = mstrdup(line);
    char **w;
    size_t n = split(copy, &w);
    mjd_text_cx cx = {x->node, jd_view(x), x->peered, unix_ms};
    bool read = true, ok = false;
    char *text = NULL;
    mj_job_id id;
    if (n == 1 && !strcmp(w[0], "jobs")) {
        text = mjd_jobs_text(&cx);
        ok = true;
    } else if (n == 3 && !strcmp(w[0], "job") && !strcmp(w[1], "status")) {
        if (!mj_parse_job(w[2], &id)) text = bad_id(w[2]);
        else ok = mjd_status_text(&cx, id, &text);
    } else if (n >= 3 && !strcmp(w[0], "job") && !strcmp(w[1], "results")) {
        uint64_t from, cnt;
        if (!mj_parse_job(w[2], &id)) text = bad_id(w[2]);
        else if (number(w + 3, n - 3, 0, 0, &from, &text) && number(w + 3, n - 3, 1, MESH_JOBS_RESULTS_MAX, &cnt, &text))
            ok = mjd_results_text(&cx, id, from, cnt < MESH_JOBS_RESULTS_MAX ? cnt : MESH_JOBS_RESULTS_MAX, &text);
    } else read = false;
    if (read) {
        answer(x, reply, ok, text ? text : "");
        free(text);
        goto done;
    }
    mj_req req;
    char *err = NULL;
    if (n == 0 || strcmp(w[0], "job")) {
        char *q = mdebug(line);
        err = mfmt("not a job request: %s", q);
        free(q);
    } else if (mj_req_parse((const char *const *)w + 1, n - 1, &req, &err)) {
        if (mj_node_leading(x->node) || !x->peered) {
            x->token++;
            jd_reply *slot = momap_put1(x->replies, x->token, NULL);
            *slot = *reply;
            reply->r = NULL;
            mj_outs o = mj_node_request(x->node, req, x->token, now, unix_ms);
            mj_outs_append(outs, &o);
            mj_outs_free(&o);
            goto done;
        }
        mj_req_free(&req);
        /* Another member leads: the fleet thread forwards it (F_CTL) and routes the answer. */
        if (x->sh->peer && reply->k == MESH_JOB_REPLY_LOCAL) {
            mesh_peer_event e = mesh_peer_event_new(MESH_PEV_CHANGE);
            e.line = mstrdup(line);
            e.reply = reply->r;
            reply->r = NULL;
            if (mesh_chan_send(x->sh->peer, &e) != MESH_CHAN_OK) mesh_peer_event_free(&e);
            goto done;
        }
        err = mfmt("jobs: system %u does not lead the job table (the fleet's leader does)", x->d->cfg.me);
    }
    answer(x, reply, false, err ? err : "");
    free(err);
done:
    free(w);
    free(copy);
}

/* ---- events */

static void event(jd *x, mesh_job_event *e, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    mj_outs o = {0};
    switch (e->k) {
    case MESH_JEV_FRAME: {
        mj_frame f;
        char *err = NULL;
        if (mj_frame_decode(e->ty, e->body, e->len, &f, &err)) o = mj_node_recv(x->node, e->from, f, now, unix_ms);
        else {
            mesh_error("jobs: a frame from system %u: %s", e->from, err ? err : "");
            free(err);
        }
        break;
    }
    case MESH_JEV_DONE: {
        mj_item_res r = e->res;
        memset(&e->res, 0, sizeof e->res);
        /* A payload: its blob named and handed to blobd before the result (staged until its copies are durable). */
        if (e->has_payload && r.status == MESH_JOBS_RS_OK) {
            mj_hash h;
            mesh_sha256(e->payload, e->payload_len, h.b);
            r.has_blob = true;
            r.blob = (mj_blob_ref){h, (uint32_t)e->payload_len, 0};
            blobs_send(x, (mjd_bev){.k = MJD_BEV_LOCAL, .job = r.job, .hash = h, .bytes = e->payload, .n = e->payload_len});
            e->payload = NULL;
            e->has_payload = false;
        }
        o = mj_node_result(x->node, r, now);
        break;
    }
    case MESH_JEV_BLOBS_STORED: {
        const mb_refs *hs = e->msg;
        for (size_t i = 0; hs && i < hs->n; i++) {
            mj_outs b = mj_node_blob_stored(x->node, hs->v[i].job, hs->v[i].h, now, unix_ms);
            mj_outs_append(&o, &b);
            mj_outs_free(&b);
        }
        break;
    }
    case MESH_JEV_BLOB_ACKED: {
        const mb_ref *r = e->msg;
        if (r) mj_node_blob_acked(x->node, e->from, r->job, r->h, e->ok);
        break;
    }
    case MESH_JEV_BLOB_GONE: {
        const mb_refs *hs = e->msg;
        for (size_t i = 0; hs && i < hs->n; i++) mj_node_blob_gone(x->node, hs->v[i].job, &hs->v[i].h);
        break;
    }
    case MESH_JEV_CTL: {
        jd_reply r = {e->reply_to, e->reply, e->remote_from, e->remote_id};
        e->reply = NULL;
        ctl(x, e->line ? e->line : "", &r, now, unix_ms, &o);
        jd_reply_free(&r);
        break;
    }
    }
    mj_outs_append(out, &o);
    mj_outs_free(&o);
}

/* The job programs running here: asked every 250 ms, the answer picked up when the driver gives it
 * (never waited for). */
static void progs(jd *x) {
    mesh_shared *sh = x->sh;
    if (x->progs) {
        mesh_result r;
        int w = mesh_reply_poll(x->progs, &r);
        if (w == MESH_CHAN_OK) {
            /* Backpressure: while blobd holds too many of our payloads (their copies not durable yet), no new leases. */
            uint64_t mem = atomic_load_explicit(&sh->blob_mem, memory_order_relaxed);
            bool full = mem > MESH_BLOBD_BLOB_MEM_MAX;
            if (full != x->throttled) {
                x->throttled = full;
                mesh_info("jobs: %s leases (%llu MiB of payloads wait for their copies)", full ? "no new" : "taking", (unsigned long long)(mem >> 20));
            }
            mesh_job_progs *p = r.ok ? r.ptr : NULL;
            if (p && !full) {
                mj_node_set_progs(x->node, p->v, p->n);
                p->v = NULL;
                p->n = 0;
            } else if (p) mj_node_set_progs(x->node, NULL, 0);
            mesh_result_free(&r);
        }
        if (w != MESH_CHAN_EMPTY) {
            mesh_reply_release(x->progs);
            x->progs = NULL;
        }
    }
    if (!x->progs && mesh_mono_ms() - x->progs_at >= MESH_JOBD_PROGS_MS) {
        x->progs_at = mesh_mono_ms();
        mesh_reply *r = mesh_reply_new();
        mesh_cmd c = mesh_cmd_new(MESH_CMD_JOB_PROGS);
        c.reply = mesh_reply_sender(r);
        if (mesh_chan_send(sh->cmd, &c) == MESH_CHAN_OK) x->progs = r;
        else {
            mesh_cmd_free(&c);
            mesh_reply_release(r);
        }
    }
}

static void hist_free(void *p) {
    jd_hist *h = p;
    free(h->at);
    free(h->done);
}
static void ranges_free(void *p) { mj_ranges_free(p); }

void *mjd_jobs_main(void *arg) {
    mjd *d = arg;
    mesh_shared *sh = d->sh;
    jd x = {.d = d, .sh = sh, .peered = sh->peer != NULL};
    x.t0 = mesh_mono_ms();
    x.disk = mjd_store_disk(d->store);
    size_t njobs = momap_len(d->rep.committed.jobs);
    if (x.peered)
        mesh_info("jobs: system %u of %u: the job table %s (%zu job(s)); led by the fleet's leader, committed on %u of %u", d->cfg.me, d->cfg.n,
                  mjd_store_persisted(d->store) ? "is persisted" : "is kept in memory (no --jobs-state)", njobs, d->cfg.quorum, d->cfg.n);
    else
        mesh_info("jobs: system %u of %u: the job table %s (%zu job(s)); this meshd leads it", d->cfg.me, d->cfg.n,
                  mjd_store_persisted(d->store) ? "is persisted" : "is kept in memory (no --jobs-state)", njobs);
    x.node = mj_node_new(d->cfg, d->rep);
    memset(&d->rep, 0, sizeof d->rep);
    x.replies = momap_new(1, sizeof(jd_reply), jd_reply_free, NULL);
    x.hist = momap_new(1, sizeof(jd_hist), hist_free, NULL);
    x.sent = momap_new(1, sizeof(mj_ranges), ranges_free, NULL);
    x.told = momap_new(1, 1, NULL, NULL);
    x.progs_at = mesh_mono_ms() - 1000;
    x.publish_at = mesh_mono_ms();
    for (;;) {
        /* Fail-stop (mj_durable): this thread does nothing more. */
        if (atomic_load(&sh->fatal) || atomic_load(&d->stop)) break;
        mesh_job_event first;
        int w = mesh_chan_recv(sh->jobs, &first, 5);
        if (w == MESH_CHAN_CLOSED) break;
        uint64_t now = jd_now(&x), unix_ms = jd_unix_ms();
        mj_outs out = mj_node_set_view(x.node, jd_view(&x), now, unix_ms);
        apply(&x, &out);
        if (w == MESH_CHAN_OK) {
            size_t k = 0;
            mesh_job_event e = first;
            do {
                mj_outs o = {0};
                event(&x, &e, now, unix_ms, &o);
                mesh_job_event_free(&e);
                apply(&x, &o);
            } while (++k < 4096 && mesh_chan_try_recv(sh->jobs, &e) == MESH_CHAN_OK);
        }
        progs(&x);
        /* The fleet scheduler's model for the lease choice (mj_select), refreshed every second. */
        mjd_ext *ext = d->ext;
        pthread_mutex_lock(&ext->mu);
        mj_sched *cx = ext->sched;
        ext->sched = NULL;
        pthread_mutex_unlock(&ext->mu);
        if (cx) mj_node_set_sched(x.node, cx);
        out = mj_node_tick(x.node, now, unix_ms);
        apply(&x, &out);
        if (mesh_mono_ms() - x.publish_at >= MESH_JOBD_PUBLISH_MS) {
            x.publish_at = mesh_mono_ms();
            jd_publish(&x);
            if (mjd_store_wants_compact(d->store)) {
                /* A failed compaction leaves the log as it was (the rewrite goes through a renamed file): appends go on. */
                char *err = NULL;
                if (!mjd_store_rewrite(d->store, &x.node->rep, &err)) mesh_error("jobs: cannot compact the job log: %s", err ? err : "");
                free(err);
            }
        }
    }
    if (x.progs) mesh_reply_release(x.progs);
    momap_free(x.replies);
    momap_free(x.hist);
    momap_free(x.sent);
    momap_free(x.told);
    mj_node_free(x.node);
    return NULL;
}
