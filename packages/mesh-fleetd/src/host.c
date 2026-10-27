/* The relay's host (mpr_host) over mesh_shared: what peer.rs reads and writes of Shared, and what it
 * asks the driver and the catalog. */
#include "fleetd_int.h"

#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/jobd/jobd.h"
#include "mesh/loader.h"
#include "mesh/peer_link.h"
#include "mesh/log.h"
#include "mesh/rsfmt.h"
#include "mesh/views.h"

#include <stdlib.h>
#include <string.h>

/* ---- driver commands */

bool fd_ask(mesh_shared *sh, mesh_cmd c, int64_t within_ms, mesh_result *out, char **err) {
    mesh_reply *r = mesh_reply_new();
    mesh_reply_drop(c.reply);
    c.reply = mesh_reply_sender(r);
    if (mesh_chan_send(sh->cmd, &c) != MESH_CHAN_OK) {
        mesh_cmd_free(&c);
        mesh_reply_release(r);
        *err = mesh_xstrdup("driver gone");
        return false;
    }
    int w = mesh_reply_wait(r, within_ms, out);
    mesh_reply_release(r);
    if (w != MESH_CHAN_OK) {
        mesh_buf b = MESH_BUF_INIT;
        mesh_buf_puts(&b, "no answer from the driver in ");
        mesh_rs_duration_ms(&b, (uint64_t)within_ms);
        *err = mesh_buf_take(&b);
        return false;
    }
    if (!out->ok) {
        *err = out->text ? out->text : mesh_xstrdup("");
        out->text = NULL;
        mesh_result_free(out);
        return false;
    }
    return true;
}

/* The driver's inventory as fleet.rs Actuals (none when the driver does not answer in time). */
mfl_actual *fd_inventory(mesh_shared *sh, int64_t within_ms, size_t *n) {
    *n = 0;
    mesh_result x;
    char *err = NULL;
    if (!fd_ask(sh, mesh_cmd_new(MESH_CMD_INVENTORY), within_ms, &x, &err)) {
        free(err);
        return NULL;
    }
    const mesh_actuals *a = x.ptr;
    mfl_actual *v = NULL;
    if (a && a->n) {
        v = mesh_xcalloc(a->n, sizeof *v);
        for (size_t i = 0; i < a->n; i++) {
            const mesh_actual *y = &a->v[i];
            v[i] = mfl_actual_new(y->name, y->dir, y->hash, y->has_prev, y->prev, y->run);
            v[i].failed = y->failed;
            v[i].lockstep = y->lockstep;
        }
        *n = a->n;
    }
    mesh_result_free(&x);
    return v;
}

/* ---- the status block and the mailbox */

#define F(c) ((mesh_fleetd *)(c))
#define SH(c) (F(c)->sh)

static void host_u32(void *c, size_t off, uint32_t v) {
    mesh_shared *sh = SH(c);
    mesh_host_block_set_u32(&sh->host, off, v);
    if (off != MESH_S_LINKED) return;
    /* The links' frame queues follow the linked members (peer.rs sets link_out / bulk_out on a link,
     * and a link's end drops them): mesh_shared_send_peer refuses a member that is not linked. */
    pthread_mutex_lock(&sh->mu);
    for (uint32_t k = 0; k < sh->n && k < MESH_MAX_SYSTEMS; k++) {
        if (k == sh->me) continue;
        bool on = v >> k & 1;
        if (on && !sh->link_out[k]) {
            sh->link_out[k] = mesh_link_queue_new(MPN_LINK_QUEUE);
            sh->bulk_out[k] = mesh_link_queue_new(MPN_BULK_QUEUE);
        } else if (!on && sh->link_out[k]) {
            mesh_chan_release(sh->link_out[k]);
            mesh_chan_release(sh->bulk_out[k]);
            sh->link_out[k] = sh->bulk_out[k] = NULL;
        }
    }
    pthread_mutex_unlock(&sh->mu);
}
static void host_u64(void *c, size_t off, uint64_t v) { mesh_host_block_set_u64(&SH(c)->host, off, v); }
static void host_str(void *c, size_t off, size_t cap, const char *s) { mesh_host_block_set_str(&SH(c)->host, off, cap, s); }
static void host_bytes(void *c, size_t off, const uint8_t *b, size_t n) {
    mesh_host_field f = {off, b, n};
    mesh_host_block_set_many(&SH(c)->host, &f, 1);
}
static void host_read(void *c, size_t off, uint8_t *out, size_t n) {
    memset(out, 0, n);
    if (off >= MESH_SNAP_HOST) return;
    if (n > MESH_SNAP_HOST - off) n = MESH_SNAP_HOST - off;
    uint8_t *b = mesh_xmalloc(MESH_SNAP_HOST);
    mesh_host_block_bytes(&SH(c)->host, b);
    memcpy(out, b + off, n);
    free(b);
}
static uint64_t uptime_ms(void *c) { return mesh_host_block_uptime_ms(&SH(c)->host); }
static uint64_t mb_get64(void *c, size_t off) { return mesh_mailbox_get64(SH(c)->mb, off); }
static uint32_t mb_get32(void *c, size_t off) { return mesh_mailbox_get(SH(c)->mb, off); }
static void mb_read(void *c, size_t off, uint8_t *out, size_t n) { mesh_mailbox_read(SH(c)->mb, off, out, n); }
static void mb_write(void *c, size_t off, const uint8_t *b, size_t n) { mesh_mailbox_write(SH(c)->mb, off, b, n, n); }
static void mb_publish64(void *c, size_t off, uint64_t v) { mesh_mailbox_publish64(SH(c)->mb, off, v); }
static void lock_feed(void *c, bool on) { atomic_store(&SH(c)->lock_feed, on); }

/* peer.rs round_stats: the member's round counter, our rounds' trips and the wait for its round. */
static void round_stats(void *c, uint32_t k, uint64_t n, double rtt_us, double wait_us) {
    mesh_shared *sh = SH(c);
    if (k >= MESH_MAX_SYSTEMS) return;
    atomic_store(&sh->round_of[k], n);
    pthread_mutex_lock(&sh->mu);
    if (rtt_us >= 0) {
        sh->rtt[k].sum += rtt_us;
        sh->rtt[k].n++;
    }
    sh->wait[k].sum += wait_us;
    sh->wait[k].n++;
    pthread_mutex_unlock(&sh->mu);
}

/* ---- the reconciler */

static void committed(void *c, const mfd_doc *doc) {
    mesh_fleetd *f = c;
    if (f->rec) mrc_post(f->rec, doc);
}

static void recon(void *c, mpr_recon *out) {
    mesh_fleetd *f = c;
    pthread_mutex_lock(&f->mu);
    *out = (mpr_recon){.applied = f->applied, .status = f->status, .reason = mesh_xstrdup(f->reason ? f->reason : "")};
    if (f->nmoves) {
        out->moves = mesh_xcalloc(f->nmoves, sizeof *out->moves);
        for (size_t i = 0; i < f->nmoves; i++) {
            out->moves[i] = f->moves[i];
            out->moves[i].name = mesh_xstrdup(f->moves[i].name);
        }
        out->nmoves = f->nmoves;
    }
    pthread_mutex_unlock(&f->mu);
}

/* ---- the catalog */

/* peer.rs Relay::catalog: the leader's view of an image (its own catalog). */
static bool catalog(void *c, const char *dir, mfe_image *out, char **err) {
    mesh_shared *sh = SH(c);
    mesh_payload p;
    mesh_manifest m;
    uint64_t hash;
    if (!mesh_loader_identity(sh->programs, dir, &p, &m, &hash, err)) return false;
    mesh_payload_free(&p);
    bool ok = false;
    char *name = NULL;
    if (m.abi != MESH_RT_ABI) *err = mesh_buf_fmt("abi: %s declares abi %u, meshd runs %u", m.name, m.abi, MESH_RT_ABI);
    else if ((name = mesh_catalog_name(dir, err))) {
        memset(out, 0, sizeof *out);
        out->dir = name;
        out->name = mesh_xstrdup(m.name);
        out->hash = hash;
        out->lockstep = m.peer == MESH_PEER_MODE_LOCKSTEP;
        for (size_t i = 0; i < m.nbackends; i++) mt_push(&out->backends, m.backends[i]);
        out->autostart = m.autostart;
        if (m.place) {
            out->placeable = true;
            out->place_cap = m.place->cap;
            out->place_fanout = m.fanout;
        }
        mesh_needs_copy(&out->needs, &m.needs);
        ok = true;
    }
    mesh_manifest_free(&m);
    return ok;
}

static mfl_actual *inventory(void *c, size_t *n) { return fd_inventory(SH(c), 5000, n); }

/* peer.rs serve_image: this member's catalog image of dir, if it is name at hash. */
static bool image(void *c, const char *dir, const char *name, uint64_t hash, mpr_payload *out, char **err) {
    mesh_shared *sh = SH(c);
    mesh_payload p;
    mesh_manifest m;
    uint64_t h;
    if (!mesh_loader_identity(sh->programs, dir, &p, &m, &h, err)) return false;
    bool ok = false;
    if (strcmp(m.name, name) != 0) *err = mesh_buf_fmt("system %u: its catalog's %s is %s, not %s", sh->me, dir, m.name, name);
    else if (h != hash) *err = mesh_buf_fmt("system %u: its catalog's %s is %016llx, not %016llx", sh->me, dir, (unsigned long long)h, (unsigned long long)hash);
    else {
        memset(out, 0, sizeof *out);
        out->dir = p.dir, p.dir = NULL;
        out->manifest = p.manifest, p.manifest = NULL;
        mpw_put(&out->ptx, p.ptx, p.ptx_len);
        ok = true;
    }
    mesh_payload_free(&p);
    mesh_manifest_free(&m);
    return ok;
}

/* peer_sched.rs's view-out thread: stop the program here, then its view from the driver. */
static bool stop_and_view(void *c, const char *name, uint32_t *view_abi, mpw_bytes *view, char **err) {
    mesh_shared *sh = SH(c);
    mesh_cmd run = mesh_cmd_new(MESH_CMD_PROG_RUN);
    run.name = mesh_xstrdup(name);
    run.on = false;
    char *t = NULL;
    if (!mesh_shared_ask(sh, run, 30000, &t)) {
        *err = t;
        return false;
    }
    free(t);
    mesh_cmd get = mesh_cmd_new(MESH_CMD_VIEW_GET);
    get.name = mesh_xstrdup(name);
    mesh_result x;
    char *e = NULL;
    if (!fd_ask(sh, get, 10000, &x, &e)) {
        /* The Rust's words for a view that did not come. */
        if (strncmp(e, "no answer from the driver", 25) == 0) {
            free(e);
            e = mesh_xstrdup("no view from the driver");
        }
        *err = e;
        return false;
    }
    const mesh_view_bytes *v = x.ptr;
    *view_abi = v ? v->view_abi : 0;
    memset(view, 0, sizeof *view);
    if (v && v->len) mpw_put(view, v->bytes, v->len);
    mesh_result_free(&x);
    return true;
}

/* peer.rs send_view: the fleet's view for the driver's placement. */
static void driver_view(void *c, uint32_t up, bool has_leader, uint32_t leader, const mfd_doc *places) {
    mesh_shared *sh = SH(c);
    mesh_cmd v = mesh_cmd_new(MESH_CMD_FLEET_VIEW);
    v.up = up;
    v.has_leader = has_leader;
    v.leader = leader;
    if (places && places->nplaces) {
        v.places = mesh_xcalloc(places->nplaces, sizeof *v.places);
        for (size_t i = 0; i < places->nplaces; i++) {
            mesh_place_req *q = &v.places[i];
            q->name = mesh_xstrdup(places->place_name[i]);
            q->nwords = places->place_words[i].n;
            q->words = mesh_xcalloc(q->nwords + 1, sizeof *q->words);
            for (size_t j = 0; j < q->nwords; j++) q->words[j] = mesh_xstrdup(places->place_words[i].v[j]);
        }
        v.nplaces = places->nplaces;
    }
    if (mesh_chan_send(sh->cmd, &v) != MESH_CHAN_OK) mesh_cmd_free(&v);
}

/* ---- frames for others */

static void place_free(void *m) {
    mpw_place_free(m);
    free(m);
}
static void frame_free(void *m) {
    mpw_frame_free(m);
    free(m);
}

/* F_PLACE / F_FRAME / F_INPUT straight to the driver (peer.rs's reader), decoded (the relay checked them). */
static void place_frame(void *c, uint32_t from, mpw_bytes f) {
    mesh_shared *sh = SH(c);
    const uint8_t *b = f.b + 1;
    char *err = NULL;
    mesh_cmd cmd;
    bool ok = false;
    size_t rest;
    switch (f.b[0]) {
    case MESH_F_PLACE: {
        mpw_place *m = mesh_xcalloc(1, sizeof *m);
        if (mpw_place_rest_len(b, &rest, &err) && mpw_place_decode(b, b + MPW_PLACE_HEAD, rest, m, &err)) {
            cmd = mesh_cmd_new(MESH_CMD_PEER_PLACE);
            cmd.msg = m, cmd.msg_free = place_free, cmd.from = from;
            ok = true;
        } else free(m);
        break;
    }
    case MESH_F_FRAME: {
        mpw_frame *m = mesh_xcalloc(1, sizeof *m);
        if (mpw_frame_rest_len(b, &rest, &err) && mpw_frame_decode(b, b + MPW_FRAME_HEAD, rest, m, &err)) {
            cmd = mesh_cmd_new(MESH_CMD_PEER_FRAME);
            cmd.msg = m, cmd.msg_free = frame_free, cmd.from = from;
            ok = true;
        } else free(m);
        break;
    }
    case MESH_F_INPUT: {
        char name[MESH_NAME_BYTES + 1];
        if (mpw_input_len(b, &rest, &err) && mpw_name_of(b, MESH_NAME_BYTES, name, &err)) {
            cmd = mesh_cmd_new(MESH_CMD_PEER_INPUT);
            cmd.name = mesh_xstrdup(name);
            cmd.bytes = mesh_xmalloc(rest ? rest : 1);
            memcpy(cmd.bytes, b + MPW_INPUT_HEAD, rest);
            cmd.len = rest;
            ok = true;
        }
        break;
    }
    default: err = mesh_buf_fmt("bad frame type %u", f.b[0]);
    }
    if (ok) {
        if (mesh_chan_send(sh->cmd, &cmd) != MESH_CHAN_OK) mesh_cmd_free(&cmd);
    } else {
        mesh_error("peer: system %u: %s", from, err ? err : "");
        free(err);
    }
    mpw_bytes_free(&f);
}

static void jobs_frame(void *c, uint32_t from, uint8_t type, const uint8_t *body, size_t n) { mjd_jobs_frame(SH(c), from, type, body, n); }
static void blobs_frame(void *c, uint32_t from, uint8_t type, const uint8_t *body, size_t n) { mjd_blobs_frame(SH(c), from, type, body, n); }

/* The bridge's answer to a change: the control port's (or the jobs thread's) one-shot reply. */
void fd_reply_fn(void *ctx, bool ok, const char *text) {
    if (!ctx) return;
    if (ok) mesh_reply_ok(ctx, text);
    else mesh_reply_err(ctx, text);
}

typedef struct {
    mesh_reply *r;
    mpr_reply to;
} pump_t;

/* A local `job ...` answer for someone else's mpr_reply: waits for the jobs thread's. */
static void *pump(void *p) {
    pump_t *x = p;
    mesh_result res;
    if (mesh_reply_wait(x->r, FD_APPLY_MS, &res) == MESH_CHAN_OK) {
        x->to.fn(x->to.ctx, res.ok, res.text ? res.text : "");
        mesh_result_free(&res);
    } else x->to.fn(x->to.ctx, false, "jobs thread gone");
    mesh_reply_release(x->r);
    free(x);
    return NULL;
}

/* A `job ...` request for the jobs thread (JobEvent::Ctl): local with its reply, or member from's. */
static void jobs_ctl(void *c, const char *line, bool remote, uint32_t from, uint32_t id, mpr_reply reply) {
    mesh_shared *sh = SH(c);
    mesh_job_event e = mesh_job_event_new(MESH_JEV_CTL);
    e.line = mesh_xstrdup(line);
    if (remote) {
        e.reply_to = MESH_JOB_REPLY_REMOTE;
        e.remote_from = from;
        e.remote_id = id;
    } else if (reply.fn == fd_reply_fn) {
        /* The bridge's reply: the jobs thread answers the waiter directly. */
        e.reply_to = MESH_JOB_REPLY_LOCAL;
        e.reply = reply.ctx;
    } else if (reply.fn) {
        mesh_reply *r = mesh_reply_new();
        e.reply_to = MESH_JOB_REPLY_LOCAL;
        e.reply = mesh_reply_sender(r);
        pump_t *x = mesh_xmalloc(sizeof *x);
        *x = (pump_t){r, reply};
        pthread_t t;
        if (pthread_create(&t, NULL, pump, x) == 0) pthread_detach(t);
        else {
            mesh_reply_release(r);
            free(x);
        }
    }
    if (mesh_chan_send(sh->jobs, &e) != MESH_CHAN_OK) {
        if (e.reply_to == MESH_JOB_REPLY_LOCAL && e.reply) {
            mesh_reply_err(e.reply, "jobs thread gone");
            e.reply = NULL;
        }
        mesh_job_event_free(&e);
    }
}

static void jobs_view(void *c, uint64_t term, bool has_leader, uint32_t leader, uint32_t alive, uint32_t linked) {
    mjd_set_view(SH(c), (mj_view){term, has_leader, leader, alive, linked});
}
static void jobs_sched(void *c, const msm_member *members, size_t n, const mfd_doc *doc, const msm_perfs *perf, const msw_weights *w) {
    mjd_jobs_sched(SH(c), members, n, doc, perf, w);
}

/* ---- texts */

static void texts(void *c, const char *fleet, const char *groups) {
    mesh_shared *sh = SH(c);
    mesh_shared_set_text(sh, &sh->fleet_text, fleet);
    mesh_shared_set_text(sh, &sh->groups_text, groups);
}

static void sched_texts(void *c, const char *all, const msp_explains *explains, const char *dry, const char *records) {
    mesh_shared *sh = SH(c);
    mesh_explain *ex = explains->n ? mesh_xcalloc(explains->n, sizeof *ex) : NULL;
    for (size_t i = 0; i < explains->n; i++) {
        ex[i].name = mesh_xstrdup(explains->v[i].program);
        ex[i].text = msp_explain_text(&explains->v[i]);
    }
    mesh_recs recs = {0};
    mesh_views_parse(records, &recs);
    pthread_mutex_lock(&sh->mu);
    free(sh->sched_all);
    sh->sched_all = mesh_xstrdup(all);
    free(sh->sched_dry);
    sh->sched_dry = mesh_xstrdup(dry);
    for (size_t i = 0; i < sh->nexplain; i++) {
        free(sh->sched_explain[i].name);
        free(sh->sched_explain[i].text);
    }
    free(sh->sched_explain);
    sh->sched_explain = ex;
    sh->nexplain = explains->n;
    mesh_recs old = sh->fleet_view;
    sh->fleet_view = recs;
    pthread_mutex_unlock(&sh->mu);
    mesh_recs_free(&old);
}

static void peer_state(void *c, uint32_t s) { mesh_shared_set_peer_state(SH(c), s); }

static void net_count(void *c, uint32_t k, bool tx, uint8_t type, size_t bytes) {
    mesh_shared *sh = SH(c);
    if (k >= MESH_MAX_SYSTEMS) return;
    if (tx) mesh_net_counters_tx(&sh->host.net, k, &type, bytes);
    else mesh_net_counters_rx(&sh->host.net, k, type, bytes);
}

static void logline(void *c, bool error, const char *line) {
    (void)c;
    if (error) mesh_error("%s", line);
    else mesh_info("%s", line);
}

static void fail_stop(void *c, const char *why) { mesh_shared_fail_stop(SH(c), why); }

mpr_host fd_host(mesh_fleetd *f) {
    return (mpr_host){
        .ctx = f,
        .host_u32 = host_u32,
        .host_u64 = host_u64,
        .host_str = host_str,
        .host_bytes = host_bytes,
        .host_read = host_read,
        .uptime_ms = uptime_ms,
        .mb_get64 = mb_get64,
        .mb_get32 = mb_get32,
        .mb_read = mb_read,
        .mb_write = mb_write,
        .mb_publish64 = mb_publish64,
        .lock_feed = lock_feed,
        .round_stats = round_stats,
        .committed = committed,
        .recon = recon,
        .catalog = catalog,
        .inventory = inventory,
        .image = image,
        .stop_and_view = stop_and_view,
        .driver_view = driver_view,
        .place_frame = place_frame,
        .jobs_frame = jobs_frame,
        .blobs_frame = blobs_frame,
        .jobs_ctl = jobs_ctl,
        .jobs_view = jobs_view,
        .jobs_sched = jobs_sched,
        .texts = texts,
        .sched_texts = sched_texts,
        .peer_state = peer_state,
        .net_count = net_count,
        .log = logline,
        .fail_stop = fail_stop,
    };
}
