/* meshd's shared state, its messages, and the requests other threads make of the driver and the fleet. */
#include "mesh/shared.h"
#include "mesh/buf.h"
#include "mesh/log.h"
#include "mesh/rsfmt.h"

#include <stdlib.h>
#include <string.h>

/* ---- Src ---- */

mesh_src mesh_src_catalog(const char *dir) {
    mesh_src s;
    memset(&s, 0, sizeof s);
    s.dir = mesh_xstrdup(dir);
    return s;
}

mesh_src mesh_src_bytes(const mesh_payload *p, uint64_t hash) {
    mesh_src s;
    memset(&s, 0, sizeof s);
    s.is_bytes = true;
    mesh_payload_copy(&s.payload, p);
    s.hash = hash;
    return s;
}

mesh_src mesh_src_copy(const mesh_src *s) {
    if (s->is_bytes) return mesh_src_bytes(&s->payload, s->hash);
    return mesh_src_catalog(s->dir ? s->dir : "");
}

void mesh_src_free(mesh_src *s) {
    free(s->dir);
    if (s->is_bytes) mesh_payload_free(&s->payload);
    memset(s, 0, sizeof *s);
}

char *mesh_src_what(const mesh_src *s) {
    if (s->is_bytes) return mesh_buf_fmt("%s (from the peer)", s->payload.dir ? s->payload.dir : "");
    return mesh_xstrdup(s->dir ? s->dir : "");
}

/* ---- Cmd ---- */

mesh_cmd mesh_cmd_new(mesh_cmd_kind k) {
    mesh_cmd c;
    memset(&c, 0, sizeof c);
    c.k = k;
    c.run = -1;
    return c;
}

static void strv_free(char **v, size_t n) {
    for (size_t i = 0; i < n; i++) free(v[i]);
    free(v);
}

void mesh_cmd_free(mesh_cmd *c) {
    free(c->name);
    if (c->has_src) mesh_src_free(&c->src);
    free(c->bytes);
    strv_free(c->words, c->nwords);
    if (c->msg && c->msg_free) c->msg_free(c->msg);
    for (size_t i = 0; i < c->nplaces; i++) { free(c->places[i].name); strv_free(c->places[i].words, c->places[i].nwords); }
    free(c->places);
    if (c->k == MESH_CMD_JOB_LEASE) mj_lease_msg_free(&c->lease);
    mesh_reply_drop(c->reply);
    memset(c, 0, sizeof *c);
}

void mesh_actuals_free(void *p) {
    mesh_actuals *a = p;
    if (!a) return;
    for (size_t i = 0; i < a->n; i++) { free(a->v[i].name); free(a->v[i].dir); }
    free(a->v);
    free(a);
}

void mesh_view_bytes_free(void *p) {
    mesh_view_bytes *v = p;
    if (!v) return;
    free(v->bytes);
    free(v);
}

void mesh_job_progs_free(void *p) {
    mesh_job_progs *j = p;
    if (!j) return;
    mj_progs_free(j->v, j->n);
    free(j);
}

/* ---- PeerEvent, JobEvent ---- */

mesh_peer_event mesh_peer_event_new(mesh_peer_event_kind k) {
    mesh_peer_event e;
    memset(&e, 0, sizeof e);
    e.k = k;
    return e;
}

void mesh_peer_event_free(mesh_peer_event *e) {
    free(e->line);
    free(e->text);
    free(e->name);
    free(e->dir);
    if (e->msg && e->msg_free) e->msg_free(e->msg);
    mesh_reply_drop(e->reply);
    memset(e, 0, sizeof *e);
}

mesh_job_event mesh_job_event_new(mesh_job_event_kind k) {
    mesh_job_event e;
    memset(&e, 0, sizeof e);
    e.k = k;
    return e;
}

void mesh_job_event_free(mesh_job_event *e) {
    free(e->body);
    free(e->line);
    mesh_reply_drop(e->reply);
    if (e->k == MESH_JEV_DONE) mj_item_res_free(&e->res);
    free(e->payload);
    if (e->msg && e->msg_free) e->msg_free(e->msg);
    memset(e, 0, sizeof *e);
}

/* ---- link frames ---- */

static void drop_cmd(void *p) { mesh_cmd_free(p); }
static void drop_pev(void *p) { mesh_peer_event_free(p); }
static void drop_jev(void *p) { mesh_job_event_free(p); }
static void drop_frame(void *p) { free(((mesh_link_frame *)p)->b); }

mesh_chan *mesh_link_queue_new(size_t bound) { return mesh_chan_new(sizeof(mesh_link_frame), bound, drop_frame); }

/* ---- Shared ---- */

uint64_t mesh_shared_mono_us(void) {
    static _Atomic int64_t t0;
    int64_t z = 0, now = mesh_mono_us();
    atomic_compare_exchange_strong(&t0, &z, now);
    return (uint64_t)(now - atomic_load(&t0));
}

mesh_shared *mesh_shared_new(mesh_mailbox mb, const char *programs, bool peered, uint32_t me, uint32_t n, _Atomic bool *quit) {
    mesh_shared_mono_us();
    mesh_shared *sh = mesh_xcalloc(1, sizeof *sh);
    sh->mb = mb;
    mesh_host_block_init(&sh->host);
    sh->programs = mesh_xstrdup(programs);
    sh->cmd = mesh_chan_new(sizeof(mesh_cmd), 0, drop_cmd);
    sh->peer = peered ? mesh_chan_new(sizeof(mesh_peer_event), 0, drop_pev) : NULL;
    sh->jobs = mesh_chan_new(sizeof(mesh_job_event), 0, drop_jev);
    sh->blobs = NULL;
    sh->me = me;
    sh->n = n;
    atomic_init(&sh->peer_state, MESH_PEER_SINGLE);
    atomic_init(&sh->lock_feed, false);
    for (size_t k = 0; k < MESH_MAX_SYSTEMS; k++) atomic_init(&sh->round_of[k], 0);
    atomic_init(&sh->ready, false);
    atomic_init(&sh->fatal, false);
    atomic_init(&sh->blob_mem, 0);
    sh->quit = quit;
    sh->fleet_text = mesh_xstrdup("");
    sh->groups_text = mesh_xstrdup("");
    sh->sched_all = mesh_xstrdup("");
    sh->sched_dry = mesh_xstrdup("");
    pthread_mutex_init(&sh->mu, NULL);
    return sh;
}

void mesh_shared_free(mesh_shared *sh) {
    if (!sh) return;
    mesh_chan_release(sh->cmd);
    if (sh->peer) mesh_chan_release(sh->peer);
    mesh_chan_release(sh->jobs);
    for (size_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        if (sh->link_out[k]) mesh_chan_release(sh->link_out[k]);
        if (sh->bulk_out[k]) mesh_chan_release(sh->bulk_out[k]);
    }
    free(sh->programs);
    free(sh->fleet_text);
    free(sh->groups_text);
    free(sh->sched_all);
    free(sh->sched_dry);
    for (size_t i = 0; i < sh->nexplain; i++) { free(sh->sched_explain[i].name); free(sh->sched_explain[i].text); }
    free(sh->sched_explain);
    mesh_recs_free(&sh->fleet_view);
    mesh_recs_free(&sh->jobs_view_recs);
    free(sh->pipe_hist);
    pthread_mutex_destroy(&sh->mu);
    free(sh);
}

static mesh_link_frame frame_copy(const uint8_t *f, size_t n) {
    mesh_link_frame x = {mesh_xmalloc(n ? n : 1), n};
    if (n) memcpy(x.b, f, n);
    return x;
}

/* try_send of a copy to a queue held under mu; the copy is freed when refused. */
static int try_send(mesh_chan *q, const uint8_t *f, size_t n) {
    mesh_link_frame x = frame_copy(f, n);
    int r = mesh_chan_try_send(q, &x);
    if (r != MESH_CHAN_OK) free(x.b);
    return r;
}

bool mesh_shared_send_peer(mesh_shared *sh, uint32_t to, const uint8_t *frame, size_t n) {
    if (atomic_load(&sh->fatal) || to >= MESH_MAX_SYSTEMS) return false;
    pthread_mutex_lock(&sh->mu);
    mesh_chan *q = sh->link_out[to];
    int r = q ? try_send(q, frame, n) : MESH_CHAN_CLOSED;
    pthread_mutex_unlock(&sh->mu);
    return r == MESH_CHAN_OK;
}

bool mesh_shared_send_peer_wait(mesh_shared *sh, uint32_t to, const uint8_t *frame, size_t n, int64_t within_ms) {
    if (atomic_load(&sh->fatal) || to >= MESH_MAX_SYSTEMS) return false;
    int64_t t0 = mesh_mono_ms();
    for (;;) {
        pthread_mutex_lock(&sh->mu);
        mesh_chan *q = sh->link_out[to];
        int r = q ? try_send(q, frame, n) : MESH_CHAN_CLOSED;
        pthread_mutex_unlock(&sh->mu);
        if (r == MESH_CHAN_OK) return true;
        if (r != MESH_CHAN_FULL || mesh_mono_ms() - t0 >= within_ms) return false;
        mesh_sleep_us(500);
    }
}

bool mesh_shared_send_bulk(mesh_shared *sh, uint32_t to, const uint8_t *frame, size_t n) {
    if (atomic_load(&sh->fatal) || to >= MESH_MAX_SYSTEMS) return false;
    pthread_mutex_lock(&sh->mu);
    mesh_chan *q = sh->bulk_out[to];
    bool ok = q && try_send(q, frame, n) == MESH_CHAN_OK;
    /* A doorbell (an empty frame) wakes the writer if it waits on its control queue; a full one is awake. */
    if (ok && sh->link_out[to]) try_send(sh->link_out[to], NULL, 0);
    pthread_mutex_unlock(&sh->mu);
    return ok;
}

bool mesh_shared_send_mask(mesh_shared *sh, uint32_t mask, const uint8_t *frame, size_t n) {
    if (atomic_load(&sh->fatal)) return false;
    pthread_mutex_lock(&sh->mu);
    bool any = false, ok = true;
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++) {
        if (!((mask >> k) & 1u) || !sh->link_out[k]) continue;
        any = true;
        ok &= try_send(sh->link_out[k], frame, n) == MESH_CHAN_OK;
    }
    pthread_mutex_unlock(&sh->mu);
    return any && ok;
}

bool mesh_shared_broadcast(mesh_shared *sh, const uint8_t *frame, size_t n) { return mesh_shared_send_mask(sh, UINT32_MAX, frame, n); }

void mesh_shared_fail_stop(mesh_shared *sh, const char *why) {
    if (!atomic_exchange(&sh->fatal, true)) mesh_error("FATAL: %s: fail-stop (nothing more is sent; meshd exits)", why);
    atomic_store(sh->quit, true);
}

static char *within_text(const char *prefix, int64_t within_ms) {
    mesh_buf b = MESH_BUF_INIT;
    mesh_buf_puts(&b, prefix);
    mesh_rs_duration_ms(&b, (uint64_t)within_ms);
    return mesh_buf_take(&b);
}

bool mesh_shared_ask(mesh_shared *sh, mesh_cmd cmd, int64_t within_ms, char **text) {
    mesh_reply *r = mesh_reply_new();
    mesh_reply_drop(cmd.reply);
    cmd.reply = mesh_reply_sender(r);
    if (mesh_chan_send(sh->cmd, &cmd) != MESH_CHAN_OK) {
        mesh_cmd_free(&cmd);
        mesh_reply_release(r);
        *text = mesh_xstrdup("driver gone");
        return false;
    }
    mesh_result x;
    int w = mesh_reply_wait(r, within_ms, &x);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) { *text = within_text("no answer from the driver in ", within_ms); return false; }
    *text = x.text ? x.text : mesh_xstrdup("");
    x.text = NULL;
    bool ok = x.ok;
    mesh_result_free(&x);
    return ok;
}

void mesh_shared_set_peer_state(mesh_shared *sh, uint32_t s) {
    atomic_store(&sh->peer_state, s);
    mesh_host_block_set_u32(&sh->host, MESH_S_PEER_STATE, s);
}

bool mesh_shared_change(mesh_shared *sh, const char *line, int64_t within_ms, char **text) {
    if (!sh->peer) { *text = mesh_xstrdup("not in a fleet"); return false; }
    mesh_reply *r = mesh_reply_new();
    mesh_peer_event e = mesh_peer_event_new(MESH_PEV_CHANGE);
    e.line = mesh_xstrdup(line);
    e.reply = mesh_reply_sender(r);
    if (mesh_chan_send(sh->peer, &e) != MESH_CHAN_OK) {
        mesh_peer_event_free(&e);
        mesh_reply_release(r);
        *text = mesh_xstrdup("fleet thread gone");
        return false;
    }
    mesh_result x;
    int w = mesh_reply_wait(r, within_ms, &x);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) { *text = within_text("peer: no answer from the fleet in ", within_ms); return false; }
    *text = x.text ? x.text : mesh_xstrdup("");
    x.text = NULL;
    bool ok = x.ok;
    mesh_result_free(&x);
    return ok;
}

bool mesh_shared_http(mesh_shared *sh, const uint8_t *head, size_t n, uint8_t **resp, size_t *rn, char **err) {
    size_t l = 0;
    while (l < n && head[l] != '\r' && head[l] != '\n') l++;
    mesh_buf line = MESH_BUF_INIT;
    mesh_rs_lossy(&line, head, l);
    mesh_reply *r = mesh_reply_new();
    mesh_cmd c = mesh_cmd_new(MESH_CMD_REQ);
    c.name = mesh_xstrdup("http");
    c.bytes = mesh_xmalloc(n ? n : 1);
    memcpy(c.bytes, head, n);
    c.len = n;
    c.reply = mesh_reply_sender(r);
    if (mesh_chan_send(sh->cmd, &c) != MESH_CHAN_OK) {
        mesh_cmd_free(&c);
        mesh_reply_release(r);
        mesh_buf_free(&line);
        *err = mesh_xstrdup("driver gone");
        return false;
    }
    mesh_result x;
    int w = mesh_reply_wait(r, 5000, &x);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) { mesh_buf_free(&line); *err = mesh_xstrdup("GPU http timed out"); return false; }
    if (!x.ok) {
        *err = x.text ? x.text : mesh_xstrdup("");
        x.text = NULL;
        mesh_result_free(&x);
        mesh_buf_free(&line);
        return false;
    }
    mesh_host_block_add_u32(&sh->host, MESH_S_HTTP_REQUESTS, 1);
    mesh_host_block_set_str(&sh->host, MESH_S_LAST_HTTP, 64, mesh_buf_str(&line));
    mesh_buf_free(&line);
    *resp = x.bytes ? x.bytes : mesh_xmalloc(1);
    *rn = x.len;
    x.bytes = NULL;
    mesh_result_free(&x);
    return true;
}

char *mesh_shared_text(mesh_shared *sh, char *const *field) {
    pthread_mutex_lock(&sh->mu);
    char *t = mesh_xstrdup(*field ? *field : "");
    pthread_mutex_unlock(&sh->mu);
    return t;
}

void mesh_shared_set_text(mesh_shared *sh, char **field, const char *text) {
    char *t = mesh_xstrdup(text);
    pthread_mutex_lock(&sh->mu);
    free(*field);
    *field = t;
    pthread_mutex_unlock(&sh->mu);
}
