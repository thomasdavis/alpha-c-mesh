/* peer.rs: run, Relay (events, links, the core's outputs, beats, changes, the leader, policies,
 * images). */
#include "relay.h"
#include "mesh/abi/backend.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/group_state.h"
#include "mesh/abi/host_links.h"
#include "mesh/abi/host_members.h"
#include "mesh/abi/host_mload.h"
#include "mesh/abi/host_status.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/reconcile.h"
#include "mesh/fleet_groups.h"
#include "mesh/rust_fmt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CHANGE_WAIT_MS 60000u     /* a change's reconcile on the reachable members, answered within */
#define FIRST_DOC_RETRY_MS 1000u  /* a leader's first document that did not commit is proposed again */
#define FORWARD_WAIT_MS 70000u    /* a change forwarded to the leader */
#define IMAGE_WAIT_MS 30000u
#define HELLO_RESEND_MS 1000u

uint64_t mpr_mono_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

uint64_t mpr_mono_ms(void) { return mpr_mono_us() / 1000u; }

uint64_t relay_now(const mpr_relay *r) { return mpr_mono_ms() - r->t0_ms + 1; }

void relay_log(mpr_relay *r, bool error, char *line) {
    if (r->h.log) r->h.log(r->h.ctx, error, line);
    free(line);
}

void mpr_recon_free(mpr_recon *x) {
    free(x->reason);
    for (size_t i = 0; i < x->nmoves; i++) free(x->moves[i].name);
    free(x->moves);
    *x = (mpr_recon){0};
}

void mpr_payload_free(mpr_payload *p) {
    free(p->dir);
    free(p->manifest);
    mpw_bytes_free(&p->ptx);
    *p = (mpr_payload){0};
}

void waiter_done(waiter *w, bool ok, char *err) {
    pthread_mutex_lock(&w->mu);
    w->ok = ok;
    w->err = err;
    w->done = true;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mu);
}

/* ---- events */

static void post(mpr_relay *r, event e) {
    pthread_mutex_lock(&r->ev_mu);
    if (r->ev_n == r->ev_cap) {
        r->ev_cap = r->ev_cap ? r->ev_cap * 2 : 64;
        r->ev = mt_realloc(r->ev, r->ev_cap * sizeof *r->ev);
    }
    r->ev[r->ev_n++] = e;
    pthread_cond_signal(&r->ev_cv);
    pthread_mutex_unlock(&r->ev_mu);
}

static void event_free(event *e) {
    free(e->text);
    free(e->name);
    free(e->dir);
    mpw_bytes_free(&e->frame);
}

/* ---- links */

uint32_t relay_linked_mask(mpr_relay *r) {
    uint32_t m = 1u << r->me;
    pthread_mutex_lock(&r->links_mu);
    for (uint32_t k = 0; k < r->n; k++)
        if (r->links[k]) m |= 1u << k;
    pthread_mutex_unlock(&r->links_mu);
    return m;
}

bool relay_linked(mpr_relay *r, uint32_t k) {
    pthread_mutex_lock(&r->links_mu);
    bool yes = k < MAXS && r->links[k] != NULL;
    pthread_mutex_unlock(&r->links_mu);
    return yes;
}

uint64_t relay_last_rx(mpr_relay *r, uint32_t k, bool *linked) {
    pthread_mutex_lock(&r->links_mu);
    uint64_t t = 0;
    *linked = r->links[k] != NULL;
    if (*linked) t = atomic_load(&r->links[k]->last_rx);
    pthread_mutex_unlock(&r->links_mu);
    return t;
}

void relay_send(mpr_relay *r, uint32_t k, const mpw_bytes *f) {
    if (!f->n) return;
    pthread_mutex_lock(&r->links_mu);
    if (k < MAXS && r->links[k]) mpn_send(r->links[k]->l, f->b, f->n);
    pthread_mutex_unlock(&r->links_mu);
}

void relay_broadcast(mpr_relay *r, const mpw_bytes *f) {
    for (uint32_t k = 0; k < r->n; k++)
        if (k != r->me) relay_send(r, k, f);
}

bool mpr_send(mpr_relay *r, uint32_t k, const uint8_t *f, size_t n, bool bulk) {
    pthread_mutex_lock(&r->links_mu);
    bool ok = k < MAXS && r->links[k] && (bulk ? mpn_send_bulk(r->links[k]->l, f, n) : mpn_send(r->links[k]->l, f, n));
    pthread_mutex_unlock(&r->links_mu);
    return ok;
}

void mpr_broadcast(mpr_relay *r, const uint8_t *f, size_t n) {
    for (uint32_t k = 0; k < r->n; k++)
        if (k != r->me) mpr_send(r, k, f, n, false);
}

/* ---- the reader's direct frames (on the link's reader thread) */

static char *on_frame(void *ctx, uint32_t from, uint64_t lid, mpw_bytes f) {
    linkrec *lr = ctx;
    mpr_relay *r = lr->r;
    uint64_t up = r->h.uptime_ms(r->h.ctx);
    atomic_store(&lr->last_rx, up > 1 ? up : 1);
    uint8_t ty = f.b[0];
    const uint8_t *b = f.b + 1;
    char *err = NULL;
    switch (ty) {
    case MESH_F_ROUND:
        group_round(r, lr, b);
        mpw_bytes_free(&f);
        return NULL;
    case MESH_F_PLACE: {
        size_t rest;
        mpw_place m;
        if (!mpw_place_rest_len(b, &rest, &err) || !mpw_place_decode(b, b + MPW_PLACE_HEAD, rest, &m, &err)) break;
        mpw_place_free(&m);
        r->h.place_frame(r->h.ctx, from, f);
        return NULL;
    }
    case MESH_F_FRAME: {
        size_t rest;
        mpw_frame m;
        if (!mpw_frame_rest_len(b, &rest, &err) || !mpw_frame_decode(b, b + MPW_FRAME_HEAD, rest, &m, &err)) break;
        uint32_t sys = m.sys;
        mpw_frame_free(&m);
        if (sys != from) {
            err = mt_fmt("F_FRAME of system %u on system %u's link", sys, from);
            break;
        }
        r->h.place_frame(r->h.ctx, from, f);
        return NULL;
    }
    case MESH_F_INPUT: {
        char name[MESH_NAME_BYTES + 1];
        if (!mpw_name_of(b, MESH_NAME_BYTES, name, &err)) break;
        r->h.place_frame(r->h.ctx, from, f);
        return NULL;
    }
    case MESH_F_PERF: {
        uint32_t n = mpw_u32_at(b, 0);
        char *text = mt_strndup((const char *)b + 4, n);
        board_report(r, from, text);
        free(text);
        mpw_bytes_free(&f);
        return NULL;
    }
    case MESH_F_LOADREP: {
        uint64_t at = up > 1 ? up : 1;
        size_t base = mesh_mload(from);
        r->h.host_bytes(r->h.ctx, base + MESH_ML_SHARE, b, MESH_LOADREP_BYTES);
        uint8_t ab[8];
        memcpy(ab, &at, 8);
        r->h.host_bytes(r->h.ctx, base + MESH_ML_AT_MS, ab, 8);
        mpw_bytes_free(&f);
        return NULL;
    }
    default:
        if (ty >= MESH_F_JOB_AVAIL && ty <= MESH_F_RESULT) {
            r->h.jobs_frame(r->h.ctx, from, ty, b + MESH_JOB_HEAD, f.n - 1 - MESH_JOB_HEAD);
            mpw_bytes_free(&f);
            return NULL;
        }
        if (ty >= MESH_F_BLOB_PUT && ty <= MESH_F_BLOB) {
            r->h.blobs_frame(r->h.ctx, from, ty, b + MESH_JOB_HEAD, f.n - 1 - MESH_JOB_HEAD);
            mpw_bytes_free(&f);
            return NULL;
        }
        post(r, (event){.kind = EV_FRAME, .from = from, .lid = lid, .frame = f});
        return NULL;
    }
    mpw_bytes_free(&f);
    return err;
}

static void on_closed(void *ctx, uint32_t from, uint64_t lid, const char *why) {
    linkrec *lr = ctx;
    post(lr->r, (event){.kind = EV_CLOSED, .from = from, .lid = lid, .text = mt_strdup(why)});
}

static void on_count(void *ctx, uint32_t k, bool tx, uint8_t type, size_t bytes) {
    linkrec *lr = ctx;
    if (lr->r->h.net_count) lr->r->h.net_count(lr->r->h.ctx, k, tx, type, bytes);
}

static void on_linked(void *ctx, int fd, const mpk_join *join, atomic_bool *alive) {
    mpr_relay *r = ctx;
    post(r, (event){.kind = EV_LINK, .fd = fd, .from = join->system, .join = *join, .alive = alive});
}

static void on_link_log(void *ctx, const char *line) { relay_log(ctx, true, mt_strdup(line)); }

/* ---- the core's outputs */

void relay_my_status(mpr_relay *r, mpk_beat_status *out) {
    mpr_recon rs = {0};
    r->h.recon(r->h.ctx, &rs);
    uint32_t l;
    bool hl = mfc_leader(r->core, &l);
    *out = (mpk_beat_status){.has_leader = hl, .leader = hl ? l : 0, .applied = rs.applied, .status = rs.status, .up = mfc_alive(r->core, relay_now(r)),
                             .group_epoch = r->has_hello ? r->hello.epoch : 0, .group_state = atomic_load(&r->hello_state), .reason = rs.reason, .moves = rs.moves,
                             .nmoves = rs.nmoves};
    rs.reason = NULL, rs.moves = NULL, rs.nmoves = 0;
    mpr_recon_free(&rs);
}

static bool persist(void *ctx, char **err) {
    mpr_relay *r = ctx;
    if (!r->has_store) return true;
    return mfs_write(&r->store, mfc_persist(r->core), err);
}

static void done(mpr_relay *r, uint64_t v, bool ok, const char *err);

static void apply(mpr_relay *r, mfc_outs outs) {
    if (atomic_load(&r->fatal)) {
        mfc_outs_free(&outs);
        return;
    }
    char *err = NULL;
    if (!mfc_durable(&outs, persist, r, &err)) {
        char *why = mt_fmt("fleet: cannot persist the fleet state (%s)", err);
        atomic_store(&r->fatal, true);
        r->h.fail_stop(r->h.ctx, why);
        free(why), free(err);
        mfc_outs_free(&outs);
        return;
    }
    for (size_t i = 0; i < outs.n; i++) {
        mfc_out *o = &outs.v[i];
        switch (o->kind) {
        case MFC_OUT_PERSIST: break;
        case MFC_OUT_SEND: {
            mpw_bytes f;
            if (o->msg.kind == MFC_BEAT) {
                mpk_beat_status st;
                relay_my_status(r, &st);
                f = mpk_beat_frame(&o->msg, &st);
                mpk_beat_status_free(&st);
            } else f = mpk_msg_frame(&o->msg);
            relay_send(r, o->to, &f);
            mpw_bytes_free(&f);
            break;
        }
        case MFC_OUT_COMMITTED: {
            const mfd_doc *d = mfc_committed(r->core);
            relay_log(r, false, mt_fmt("fleet: version %llu committed (term %llu): %zu program(s), %zu group(s)", (unsigned long long)d->version,
                                       (unsigned long long)d->term, d->nprogs, d->ngroups));
            r->h.committed(r->h.ctx, d);
            break;
        }
        case MFC_OUT_LEADER:
            if (o->has_leader && o->leader == r->me) {
                relay_log(r, false, mt_fmt("fleet: system %u is the leader (term %llu)", r->me, (unsigned long long)mfc_term(r->core)));
                r->has_first_doc_at = false;
            } else if (o->has_leader) {
                relay_log(r, false, mt_fmt("fleet: leader is system %u (term %llu)", o->leader, (unsigned long long)mfc_term(r->core)));
            } else {
                relay_log(r, true, mt_fmt("fleet: no leader (this member hears %d of %u members; a majority is %u): changes are refused until there is one",
                                          ml_popcount(mfc_alive(r->core, relay_now(r))), r->n, mfc_quorum(mfc_cfg(r->core))));
            }
            /* Not leading: the leader's memory goes (a later leadership takes a migration over from the document). */
            if (!(o->has_leader && o->leader == r->me)) {
                ms_free(&r->paused_since);
                if (r->has_mig) mst_move_free(&r->mig.mv);
                r->has_mig = false;
            }
            break;
        case MFC_OUT_DONE: done(r, o->version, o->ok, o->text); break;
        case MFC_OUT_LOG: relay_log(r, false, mt_strdup(o->text)); break;
        }
    }
    mfc_outs_free(&outs);
}

void relay_beat_all(mpr_relay *r) {
    mfc_msg b = mfc_beat(r->core);
    mpk_beat_status st;
    relay_my_status(r, &st);
    mpw_bytes f = mpk_beat_frame(&b, &st);
    relay_broadcast(r, &f);
    mpw_bytes_free(&f);
    mpk_beat_status_free(&st);
    r->last_beat_ms = mpr_mono_ms();
}

static char *leader_word(mpr_relay *r) {
    uint32_t l;
    if (mfc_leader(r->core, &l)) return mt_fmt("system %u leads term %llu", l, (unsigned long long)mfc_term(r->core));
    if (mfc_fixed(mfc_cfg(r->core))) return mt_strdup("no leader: system 0, the pair's fixed coordinator, is unreachable (a pair has no failover)");
    return mt_fmt("no leader: this member hears %d of %u members, a majority is %u", ml_popcount(mfc_alive(r->core, relay_now(r))), r->n, mfc_quorum(mfc_cfg(r->core)));
}

void relay_reply(mpr_relay *r, replyto to, bool ok, const char *text) {
    if (!to.remote) {
        if (to.local.fn) to.local.fn(to.local.ctx, ok, text);
        return;
    }
    size_t n = strlen(text);
    if (n > MESH_F_CTL_REPLY_MAX) n = MESH_F_CTL_REPLY_MAX;
    mpw_bytes f = {0};
    uint8_t t = MESH_F_CTL_REPLY;
    mpw_put(&f, &t, 1);
    mpw_put32(&f, to.id);
    mpw_put32(&f, ok);
    mpw_put32(&f, (uint32_t)n);
    mpw_put(&f, text, n);
    relay_send(r, to.k, &f);
    mpw_bytes_free(&f);
}

void mpr_ctl_reply(mpr_relay *r, uint32_t to, uint32_t id, bool ok, const char *text) { relay_reply(r, (replyto){true, to, id, {0}}, ok, text); }

/* ---- links: install, unlink, drop */

static void unlink_member(mpr_relay *r, uint32_t k);

static void free_linkrec(linkrec *lr) {
    if (!lr) return;
    mpn_link_close(lr->l);
    while (atomic_load(&lr->refs) > 0) {
        struct timespec ts = {0, 100000};
        nanosleep(&ts, NULL);
    }
    mpn_link_free(lr->l);
    pthread_mutex_destroy(&lr->sent_mu);
    free(lr);
}

static void install(mpr_relay *r, event *e) {
    uint32_t from = e->from;
    pthread_mutex_lock(&r->links_mu);
    linkrec *old = r->links[from];
    r->links[from] = NULL;
    pthread_mutex_unlock(&r->links_mu);
    if (old) {
        relay_log(r, false, mt_fmt("peer: system %u linked again; dropping its old link", from));
        mpn_link_close(old->l);
        unlink_member(r, from);
        free_linkrec(old);
    }
    linkrec *lr = mt_alloc(sizeof *lr);
    lr->r = r;
    lr->from = from;
    lr->lid = ++r->next_lid;
    uint64_t up = r->h.uptime_ms(r->h.ctx);
    atomic_init(&lr->last_rx, up > 1 ? up : 1);
    atomic_init(&lr->last_round, 0);
    atomic_init(&lr->refs, 0);
    pthread_mutex_init(&lr->sent_mu, NULL);
    /* The link's local address (the route to this member). */
    struct sockaddr_storage la;
    socklen_t ll = sizeof la;
    char local[64] = "?";
    if (getsockname(e->fd, (struct sockaddr *)&la, &ll) == 0) {
        char host[48] = "?", serv[16] = "?";
        if (getnameinfo((struct sockaddr *)&la, ll, host, sizeof host, serv, sizeof serv, NI_NUMERICHOST | NI_NUMERICSERV) == 0) snprintf(local, sizeof local, "%s:%s", host, serv);
    }
    r->h.host_str(r->h.ctx, mesh_link(from) + MESH_L_LOCAL, 32, local);
    /* Both JOINs (the handshake ran before the link existed). */
    if (r->h.net_count) {
        r->h.net_count(r->h.ctx, from, true, MESH_F_JOIN, 1 + MESH_JOIN_BYTES);
        r->h.net_count(r->h.ctx, from, false, MESH_F_JOIN, 1 + MESH_JOIN_BYTES);
    }
    const mpk_caps *c = &e->join.caps;
    if (mesh_backend_kind(c->backend) == MESH_BACKEND_CUDA)
        relay_log(r, false, mt_fmt("peer: linked to system %u (%s, %s, cc %u.%u, %u of %u SMs, %u blocks/SM, %s)", from, c->host, c->gpu, c->cc / 10, c->cc % 10, c->sms,
                                   c->sms_total, c->blocks_per_sm, c->arch));
    else relay_log(r, false, mt_fmt("peer: linked to system %u (%s, %s, backend %u, %u cores, %s)", from, c->host, c->gpu, c->backend, c->sms_total, c->arch));
    publish_member(r, from, MESH_MS_LINKED, c);
    r->caps[from] = *c;
    r->has_caps[from] = true;
    mpn_sink sink = {lr, on_frame, on_closed, on_count};
    lr->l = mpn_link_start(e->fd, from, lr->lid, (uint32_t)r->tun.dead_ms, &sink, e->alive);
    pthread_mutex_lock(&r->links_mu);
    r->links[from] = lr;
    pthread_mutex_unlock(&r->links_mu);
    r->h.host_u32(r->h.ctx, MESH_S_LINKED, relay_linked_mask(r));
    apply(r, mfc_link(r->core, from, true, relay_now(r)));
    /* It hears from us at once: our beat and, if it is in our group, our HELLO. */
    mfc_msg b = mfc_beat(r->core);
    mpk_beat_status st;
    relay_my_status(r, &st);
    mpw_bytes f = mpk_beat_frame(&b, &st);
    relay_send(r, from, &f);
    mpw_bytes_free(&f);
    mpk_beat_status_free(&st);
    if (r->has_hello && (r->hello.mask >> from & 1) && atomic_load(&r->hello_state) == MESH_GL_HELLO) {
        mpw_bytes h = mpk_hello_frame(r->me, &r->hello);
        relay_send(r, from, &h);
        mpw_bytes_free(&h);
    }
}

/* Forget a member's link-scoped state (its HELLO, forwarded requests, image fetches). */
static void unlink_member(mpr_relay *r, uint32_t k) {
    r->has_theirs[k] = false;
    for (size_t i = 0; i < 64; i++)
        if (r->fwd[i].used && r->fwd[i].leader == k) {
            r->fwd[i].used = false;
            if (r->fwd[i].reply.fn) {
                char *t = mt_fmt("peer: the leader (system %u) was lost before it answered; the change may or may not have committed (`fleet` shows the version)", k);
                r->fwd[i].reply.fn(r->fwd[i].reply.ctx, false, t);
                free(t);
            }
        }
    for (size_t i = 0; i < 32; i++)
        if (r->images[i].used && r->images[i].from == k) {
            r->images[i].used = false;
            waiter_done(r->images[i].w, false, mt_fmt("system %u was lost before it sent the image", k));
        }
    sched_view_timeouts(r, true, k);
    apply(r, mfc_link(r->core, k, false, relay_now(r)));
    /* The group: a member lost pauses it here at once (the leader commits it paused). */
    uint32_t hs = atomic_load(&r->hello_state);
    if (r->has_hello && (r->hello.mask >> k & 1) && (hs == MESH_GL_RUNNING || hs == MESH_GL_HELLO)) {
        char *why = mt_fmt("system %u lost", k);
        group_pause(r, why);
        free(why);
    }
}

static void drop_link(mpr_relay *r, uint32_t from, const char *why) {
    pthread_mutex_lock(&r->links_mu);
    linkrec *lr = r->links[from];
    r->links[from] = NULL;
    pthread_mutex_unlock(&r->links_mu);
    if (!lr) return;
    mpn_link_close(lr->l);
    relay_log(r, true, mt_fmt("peer: link to system %u down (%s)", from, why));
    publish_member(r, from, MESH_MS_ABSENT, NULL);
    r->h.host_u32(r->h.ctx, MESH_S_LINKED, relay_linked_mask(r));
    unlink_member(r, from);
    free_linkrec(lr);
}

static bool current(mpr_relay *r, uint32_t from, uint64_t lid) {
    pthread_mutex_lock(&r->links_mu);
    bool yes = from < MAXS && r->links[from] && r->links[from]->lid == lid;
    pthread_mutex_unlock(&r->links_mu);
    return yes;
}

/* ---- images */

static void serve_image(mpr_relay *r, uint32_t k, uint32_t id, const char *name, uint64_t hash, const char *dir) {
    mpr_payload p = {0};
    char *err = NULL;
    mpw_bytes f = {0};
    uint8_t t = MESH_F_IMAGE;
    mpw_put(&f, &t, 1);
    mpw_put32(&f, id);
    uint8_t hb[8];
    memcpy(hb, &hash, 8);
    mpw_put(&f, hb, 8);
    if (r->h.image(r->h.ctx, dir, name, hash, &p, &err)) {
        size_t dl = strlen(p.dir), ml = strlen(p.manifest);
        mpw_put32(&f, 1);
        mpw_put32(&f, (uint32_t)dl);
        mpw_put32(&f, (uint32_t)ml);
        mpw_put32(&f, (uint32_t)p.ptx.n);
        mpw_put(&f, p.dir, dl);
        mpw_put(&f, p.manifest, ml);
        mpw_put(&f, p.ptx.b, p.ptx.n);
        mpr_payload_free(&p);
    } else {
        mpw_put32(&f, 0);
        mpw_put32(&f, 0);
        mpw_put32(&f, (uint32_t)strlen(err));
        mpw_put32(&f, 0);
        mpw_put(&f, err, strlen(err));
        free(err);
    }
    if (f.n > MESH_F_LOAD_MAX + MESH_IMAGE_HEAD + 1) relay_log(r, true, mt_fmt("peer: image %s for system %u is larger than F_LOAD_MAX", dir, k));
    else relay_send(r, k, &f);
    mpw_bytes_free(&f);
}

static void fetch_image(mpr_relay *r, const char *name, uint64_t hash, const char *dir, waiter *w) {
    uint32_t l, from = UINT32_MAX;
    if (mfc_leader(r->core, &l) && l != r->me && relay_linked(r, l)) from = l;
    else
        for (uint32_t k = 0; k < r->n && from == UINT32_MAX; k++)
            if (k != r->me && relay_linked(r, k)) from = k;
    if (from == UINT32_MAX) {
        waiter_done(w, false, mt_strdup("no member is linked to fetch it from"));
        return;
    }
    size_t slot = 0;
    while (slot < 32 && r->images[slot].used) slot++;
    if (slot == 32) {
        waiter_done(w, false, mt_strdup("too many image fetches in flight"));
        return;
    }
    uint32_t id = r->next_id++;
    size_t dl = strlen(dir) > 64 ? 64 : strlen(dir);
    mpw_bytes f = {0};
    uint8_t t = MESH_F_IMAGE_REQ, nm[MESH_NAME_BYTES], hb[8];
    mpw_put(&f, &t, 1);
    mpw_put32(&f, id);
    mpw_name16(name, nm);
    mpw_put(&f, nm, sizeof nm);
    memcpy(hb, &hash, 8);
    mpw_put(&f, hb, 8);
    mpw_put32(&f, (uint32_t)dl);
    mpw_put(&f, dir, dl);
    relay_send(r, from, &f);
    mpw_bytes_free(&f);
    r->images[slot] = (image_wait){true, id, w, from, mpr_mono_ms() + IMAGE_WAIT_MS};
}

static void image_reply(mpr_relay *r, uint32_t id, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < 32; i++) {
        if (!r->images[i].used || r->images[i].id != id) continue;
        r->images[i].used = false;
        waiter *w = r->images[i].w;
        size_t dl = mpw_u32_at(b, 16), ml = mpw_u32_at(b, 20), pl = mpw_u32_at(b, 24);
        const uint8_t *body = b + MESH_IMAGE_HEAD;
        if (MESH_IMAGE_HEAD + dl + ml + pl > n) {
            waiter_done(w, false, mt_strdup("F_IMAGE: short"));
            return;
        }
        if (mpw_u32_at(b, 12)) {
            w->payload.dir = mt_strndup((const char *)body, dl);
            w->payload.manifest = mt_strndup((const char *)body + dl, ml);
            mpw_put(&w->payload.ptx, body + dl + ml, pl);
            waiter_done(w, true, NULL);
        } else waiter_done(w, false, mt_strndup((const char *)body + dl, ml));
        return;
    }
}

/* ---- changes */

static void change(mpr_relay *r, char *line, mpr_reply reply) {
    uint32_t l;
    bool hl = mfc_leader(r->core, &l);
    /* A job request here, and we lead: the jobs thread (another member's leader gets it by F_CTL). */
    if (mt_starts(line, "job ") && mfc_role_of(r->core) == MFC_LEADER) {
        r->h.jobs_ctl(r->h.ctx, line, false, 0, 0, reply);
        free(line);
        return;
    }
    if (hl && l == r->me && mfc_role_of(r->core) == MFC_LEADER) {
        r->queue_line = mt_realloc(r->queue_line, (r->nqueue + 1) * sizeof *r->queue_line);
        r->queue_to = mt_realloc(r->queue_to, (r->nqueue + 1) * sizeof *r->queue_to);
        r->queue_line[r->nqueue] = line;
        r->queue_to[r->nqueue++] = (replyto){.local = reply};
        return;
    }
    if (hl && relay_linked(r, l)) {
        size_t slot = 0;
        while (slot < 64 && r->fwd[slot].used) slot++;
        uint32_t id = r->next_id++;
        size_t n = strlen(line) > MESH_F_CTL_MAX ? MESH_F_CTL_MAX : strlen(line);
        mpw_bytes f = {0};
        uint8_t t = MESH_F_CTL;
        mpw_put(&f, &t, 1);
        mpw_put32(&f, id);
        mpw_put32(&f, (uint32_t)n);
        mpw_put(&f, line, n);
        relay_send(r, l, &f);
        mpw_bytes_free(&f);
        if (slot < 64) r->fwd[slot] = (fwd_t){true, id, reply, l, mpr_mono_ms() + FORWARD_WAIT_MS};
        free(line);
        return;
    }
    char *lw = leader_word(r), *q = rf_debug_str(line);
    char *e = mt_fmt("peer: %s; the desired state is unchanged (%s refused)", lw, q);
    relay_log(r, true, mt_fmt("fleet: %s", e));
    if (reply.fn) reply.fn(reply.ctx, false, e);
    free(lw), free(q), free(e), free(line);
}

/* The leader's catalog view of an image (its own catalog). */
static bool catalog(void *ctx, const char *dir, mfe_image *out, char **err) {
    mpr_relay *r = ctx;
    return r->h.catalog(r->h.ctx, dir, out, err);
}

static bool backend_of(mpr_relay *r, uint32_t k, uint32_t *kind) {
    if (k == r->me) return *kind = mesh_backend_kind(r->my_caps.backend), true;
    if (!r->has_caps[k]) return false;
    *kind = mesh_backend_kind(r->caps[k].backend);
    return true;
}

static ml_list eligible_up(void *ctx, const mt_strs *backends) {
    mpr_relay *r = ctx;
    uint32_t alive = mfc_alive(r->core, relay_now(r)), kind;
    ml_list l = {0};
    for (uint32_t k = 0; k < r->n; k++)
        if ((alive >> k & 1) && backend_of(r, k, &kind) && mfd_can_run(backends, kind)) ml_push(&l, k);
    return l;
}

bool relay_propose(mpr_relay *r, mfd_doc doc, char *what, replyto to, uint64_t *version) {
    uint64_t now = relay_now(r), v;
    mfd_doc before = mfd_doc_clone(mfc_committed(r->core));
    mfc_outs outs;
    char *err = NULL;
    if (mfc_propose(r->core, doc, now, &v, &outs, &err)) {
        relay_log(r, false, mt_fmt("fleet: proposing version %llu: %s", (unsigned long long)v, what));
        r->has_pending = true;
        r->pending = (pending_t){.what = what, .reply = to, .version = v, .before = before};
        apply(r, outs);
        if (version) *version = v;
        return true;
    }
    char *e = mt_fmt("peer: %s; the desired state is unchanged", err);
    relay_log(r, true, mt_fmt("fleet: %s: %s", what, e));
    relay_reply(r, to, false, e);
    free(e), free(err), free(what);
    mfd_doc_free(&before);
    return false;
}

static void pending_free(pending_t *p) {
    free(p->what);
    mfd_doc_free(&p->before);
    mfd_doc_free(&p->after);
}

/* A proposal ended (the core). */
static void done(mpr_relay *r, uint64_t v, bool ok, const char *err) {
    uint32_t alive = mfc_alive(r->core, relay_now(r));
    if (!r->has_pending || r->pending.version != v) return;
    pending_t p = r->pending;
    r->has_pending = false;
    if (ok) {
        p.committed = true;
        p.up = alive;
        p.deadline = mpr_mono_ms() + CHANGE_WAIT_MS;
        p.after = mfd_doc_clone(mfc_committed(r->core));
        r->waiting = mt_realloc(r->waiting, (r->nwaiting + 1) * sizeof *r->waiting);
        r->waiting[r->nwaiting++] = p;
        return;
    }
    relay_log(r, true, mt_fmt("fleet: version %llu (%s) not committed: %s", (unsigned long long)v, p.what, err));
    char *t = mt_fmt("peer: %s; the change may still apply if a later leader holds it (`fleet` shows the version)", err);
    relay_reply(r, p.reply, false, t);
    free(t);
    pending_free(&p);
}

/* A waiting change's answer once every member that was up reconciled it (or CHANGE_WAIT passed), and
 * whether to revert it (nothing applied it anywhere). */
static bool waiting_verdict(mpr_relay *r, const pending_t *p, bool *ok, char **text, bool *revert) {
    if (!p->committed) return false;
    uint64_t v = p->version;
    mpr_recon my = {0};
    r->h.recon(r->h.ctx, &my);
    uint32_t alive = mfc_alive(r->core, relay_now(r));
    bool all = true;
    for (uint32_t k = 0; k < r->n; k++) {
        if (!((p->up & alive) >> k & 1)) continue;
        uint64_t applied = k == r->me ? my.applied : r->st[k].applied;
        if (applied < v) all = false;
    }
    if (!all && mpr_mono_ms() < p->deadline) {
        mpr_recon_free(&my);
        return false;
    }
    mt_strs parts = {0};
    int good = 0, failed = 0;
    for (uint32_t k = 0; k < r->n; k++) {
        uint64_t applied = k == r->me ? my.applied : r->st[k].applied;
        uint32_t st = k == r->me ? my.status : r->st[k].status;
        const char *why = k == r->me ? (my.reason ? my.reason : "") : (r->st[k].reason ? r->st[k].reason : "");
        if (!(p->up >> k & 1)) mt_push_own(&parts, mt_fmt("system %u absent: reconcile on join", k));
        else if (applied < v && !(alive >> k & 1)) mt_push_own(&parts, mt_fmt("system %u lost before it reconciled: reconcile on join", k));
        else if (applied < v) mt_push_own(&parts, mt_fmt("system %u pending: not reconciled in 60s", k));
        else if (st == MESH_RS_FAILED) failed++, mt_push_own(&parts, mt_fmt("system %u err %s", k, why));
        else {
            good++;
            if (!*why || !strcmp(why, "ok")) mt_push_own(&parts, mt_fmt("system %u ok", k));
            else mt_push_own(&parts, mt_fmt("system %u %s", k, why));
        }
    }
    mpr_recon_free(&my);
    char *detail = mt_join(&parts, "; ");
    mt_strs_free(&parts);
    if (good == 0 && failed > 0 && !mt_starts(p->what, "revert")) {
        *ok = false;
        *text = mt_fmt("peer err (%s): %s; reverted", detail, p->what);
        *revert = true;
    } else {
        *ok = failed == 0;
        *text = mt_fmt("version %llu %s: %s (%s)", (unsigned long long)v, failed == 0 ? "peer ok" : "peer err", p->what, detail);
        *revert = false;
    }
    free(detail);
    return true;
}

/* Committed changes: each answered once every member that was up reconciled it (or after CHANGE_WAIT). */
static void pending_check(mpr_relay *r) {
    size_t i = 0;
    while (i < r->nwaiting) {
        bool ok, revert;
        char *text;
        if (!waiting_verdict(r, &r->waiting[i], &ok, &text, &revert)) {
            i++;
            continue;
        }
        bool leading = mfc_role_of(r->core) == MFC_LEADER;
        /* A revert waits until nothing else is in flight here (nor before this term committed its first document). */
        if (revert && leading && (r->has_pending || mfc_busy(r->core) || !mfc_term_committed(r->core))) {
            free(text);
            i++;
            continue;
        }
        pending_t p = r->waiting[i];
        memmove(r->waiting + i, r->waiting + i + 1, (r->nwaiting - i - 1) * sizeof *r->waiting);
        r->nwaiting--;
        if (revert && !leading) {
            char *t = mt_fmt("%s -- not reverted: system %u no longer leads", text, r->me);
            relay_reply(r, p.reply, ok, t);
            free(t);
        } else if (revert) {
            relay_log(r, true, mt_fmt("fleet: version %llu (%s) failed on every member that tried it; reverting", (unsigned long long)p.version, p.what));
            mfd_doc back = mfe_revert(mfc_committed(r->core), &p.before, &p.after);
            relay_reply(r, p.reply, ok, text);
            relay_propose(r, back, mt_fmt("revert version %llu", (unsigned long long)p.version), (replyto){0}, NULL);
        } else {
            relay_log(r, false, mt_fmt("fleet: %s", text));
            relay_reply(r, p.reply, ok, text);
        }
        free(text);
        pending_free(&p);
    }
}

/* The first leader's programs as the first document. */
static mfd_doc seed(mpr_relay *r, uint64_t now) {
    size_t n = 0;
    mfl_actual *inv = r->h.inventory(r->h.ctx, &n);
    char **names = mt_alloc((n ? n : 1) * sizeof *names);
    mfd_prog *specs = mt_alloc((n ? n : 1) * sizeof *specs);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        mfe_image im;
        char *err = NULL;
        if (!r->h.catalog(r->h.ctx, inv[i].dir, &im, &err)) {
            relay_log(r, true, mt_fmt("fleet: seeding: %s: %s; left out", inv[i].name, err));
            free(err);
            continue;
        }
        if (im.hash != inv[i].hash) {
            relay_log(r, true, mt_fmt("fleet: seeding: %s runs %016llx here but the catalog's %s is %016llx; left out", inv[i].name, (unsigned long long)inv[i].hash,
                                      inv[i].dir, (unsigned long long)im.hash));
            mfe_image_free(&im);
            continue;
        }
        mfd_prog p = mfd_prog_new(inv[i].dir, inv[i].hash, inv[i].run, &im.needs);
        p.lockstep = im.lockstep;
        p.backends = mt_strs_clone(&im.backends);
        names[k] = inv[i].name;
        specs[k++] = p;
        mfe_image_free(&im);
    }
    ml_list up = ml_members(mfc_alive(r->core, now));
    mfd_doc d = mfl_seed(names, specs, k, &up, NULL, NULL);
    /* mfl_seed's backend lookup: the members' caps (none -> u32::MAX). */
    for (size_t gi = 0; gi < d.ngroups; gi++) {
        mfd_group *g = &d.groups[gi];
        const mfd_prog *p = mfd_prog_get(&d, g->program);
        ml_free(&g->members);
        for (size_t j = 0; j < up.n; j++) {
            uint32_t kind = UINT32_MAX;
            backend_of(r, up.v[j], &kind);
            if (mfd_can_run(&p->backends, kind)) ml_push(&g->members, up.v[j]);
        }
    }
    mt_strs ns = {0};
    for (size_t i = 0; i < d.nprogs; i++) mt_push(&ns, d.pname[i]);
    char *j = mt_join(&ns, ", ");
    relay_log(r, false, mt_fmt("fleet: seeding the desired state from system %u's programs: %s", r->me, j));
    free(j);
    mt_strs_free(&ns);
    for (size_t i = 0; i < k; i++) mfd_prog_free(&specs[i]);
    free(specs), free(names);
    for (size_t i = 0; i < n; i++) mfl_actual_free(&inv[i]);
    free(inv);
    ml_free(&up);
    return d;
}

/* The leader's policies: lockstep groups (pause, resume, auto-reform), then the scheduler. */
static void policies(mpr_relay *r, uint64_t now) {
    const mfd_doc *doc = mfc_committed(r->core);
    if (doc->version == 0) return;
    uint32_t alive = mfc_alive(r->core, now);
    for (size_t gi = 0; gi < doc->ngroups; gi++) {
        const mfd_group *g = &doc->groups[gi];
        /* A member whose data plane paused this epoch (it lost a member we may still hear) counts as missing. */
        uint32_t broken = 0;
        for (size_t i = 0; i < g->members.n; i++) {
            uint32_t k = g->members.v[i];
            uint32_t e = k == r->me ? (r->has_hello ? r->hello.epoch : 0) : r->st[k].group_epoch;
            uint32_t s = k == r->me ? atomic_load(&r->hello_state) : r->st[k].group_state;
            if ((alive >> k & 1) && e == g->epoch && s == MESH_GL_PAUSED) broken |= 1u << k;
        }
        uint32_t up = g->state == MFD_RUNNING ? alive & ~broken : alive;
        char tv[24];
        if (g->state == MFD_PAUSED) {
            if (!ms_get(&r->paused_since, g->name)) {
                snprintf(tv, sizeof tv, "%llu", (unsigned long long)now);
                ms_set(&r->paused_since, g->name, tv, true);
            }
        } else ms_del(&r->paused_since, g->name);
        const char *ps = ms_get(&r->paused_since, g->name);
        int64_t paused_ms = ps ? (int64_t)(now - strtoull(ps, NULL, 10)) : -1;
        mfd_group ng;
        if (!mfg_policy(g, up, paused_ms, &ng)) continue;
        char *what, *ml = ml_text(&g->members);
        if (g->state == MFD_RUNNING && ng.state == MFD_PAUSED) {
            ml_list al = ml_members(alive), bl = ml_members(broken);
            char *at = ml_text(&al), *bt = ml_text(&bl);
            char *extra = broken ? mt_fmt("; paused here by %s", bt) : mt_strdup("");
            what = mt_fmt("group %s paused (members %s; up %s%s)", g->name, ml, at, extra);
            free(at), free(bt), free(extra);
            ml_free(&al), ml_free(&bl);
        } else if (ml_eq(&ng.members, &g->members)) what = mt_fmt("group %s resumed: epoch %u (every member back; reset)", g->name, ng.epoch);
        else {
            char *nm = ml_text(&ng.members);
            what = mt_fmt("group %s re-formed with the survivors after %lld ms: epoch %u members %s", g->name, (long long)(paused_ms < 0 ? 0 : paused_ms), ng.epoch, nm);
            free(nm);
        }
        free(ml);
        mfd_doc d = mfd_doc_clone(doc);
        mfd_group *x = mfd_group_named(&d, g->name);
        mfd_group_free(x);
        *x = ng;
        relay_propose(r, d, what, (replyto){0}, NULL);
        return;
    }
    /* The scheduler: placement, migrations, rebalancing, group proposals. */
    sched_schedule(r, now);
}

static void lead(mpr_relay *r, uint64_t now) {
    if (mfc_busy(r->core) || r->has_pending) return;
    /* The first document of this leadership: the seed (a fresh fleet) or the accepted one re-stamped. */
    if (!mfc_term_committed(r->core)) {
        if (r->has_first_doc_at && mpr_mono_ms() - r->first_doc_at_ms < FIRST_DOC_RETRY_MS) return;
        r->has_first_doc_at = true;
        r->first_doc_at_ms = mpr_mono_ms();
        mfd_doc d;
        char *what;
        if (mfc_first_doc(r->core, &d)) {
            what = mt_fmt("version %llu (term %llu) re-committed in term %llu", (unsigned long long)d.version, (unsigned long long)d.term, (unsigned long long)mfc_term(r->core));
        } else {
            mfd_doc s = seed(r, now), empty = {0};
            mt_strs ch;
            d = sched_edit(r, &empty, s, now, &ch);
            mt_strs_free(&ch);
            what = mt_strdup("the first desired state (the first leader's programs)");
        }
        relay_propose(r, d, what, (replyto){0}, NULL);
        return;
    }
    if (r->nqueue) {
        char *line = r->queue_line[0];
        replyto to = r->queue_to[0];
        memmove(r->queue_line, r->queue_line + 1, (r->nqueue - 1) * sizeof *r->queue_line);
        memmove(r->queue_to, r->queue_to + 1, (r->nqueue - 1) * sizeof *r->queue_to);
        r->nqueue--;
        mfe_req req;
        char *err = NULL;
        if (!mfe_parse(line, &req, &err)) {
            relay_reply(r, to, false, err);
            free(err), free(line);
            return;
        }
        free(line);
        mfd_doc base = mfd_doc_clone(mfc_committed(r->core)), doc;
        mfe_ctx cx = {catalog, eligible_up, r, r->n};
        char *what = NULL;
        if (mfe_edit(&base, &req, &cx, &doc, &what, &err)) {
            /* Placed by the scheduler in the same version (a load lands where it fits; a group gets its members). */
            mt_strs ch;
            mfd_doc placed = sched_edit(r, &base, doc, now, &ch);
            if (ch.n) {
                char *j = mt_join(&ch, "; "), *w2 = mt_fmt("%s; sched: %s", what, j);
                free(j), free(what);
                what = w2;
            }
            mt_strs_free(&ch);
            relay_propose(r, placed, what, to, NULL);
        } else {
            relay_reply(r, to, false, err);
            free(err);
        }
        mfe_req_free(&req);
        mfd_doc_free(&base);
        return;
    }
    policies(r, now);
}

/* ---- the driver's view */

static void send_view(mpr_relay *r) {
    uint64_t now = relay_now(r);
    uint32_t up = mfc_alive(r->core, now), l;
    bool hl = mfc_leader(r->core, &l);
    r->h.jobs_view(r->h.ctx, mfc_term(r->core), hl, l, up, relay_linked_mask(r));
    const mfd_doc *c = mfc_committed(r->core);
    mt_buf pb = {0};
    for (size_t i = 0; i < c->nplaces; i++) {
        char *w = mt_join(&c->place_words[i], " ");
        mt_printf(&pb, "%s %s\n", c->place_name[i], w);
        free(w);
    }
    char *places = mt_take(&pb);
    bool same = r->has_view && r->view_up == up && r->view_has_leader == hl && (!hl || r->view_leader == l) && !strcmp(r->view_places, places);
    if (!same) {
        r->has_view = true;
        r->view_up = up, r->view_has_leader = hl, r->view_leader = hl ? l : 0;
        free(r->view_places);
        r->view_places = places;
        r->h.driver_view(r->h.ctx, up, hl, l, c);
        r->h.peer_state(r->h.ctx, hl ? MPR_PEER_UP : MPR_PEER_WAITING);
    } else free(places);
}

/* ---- periodic work */

static void timeouts(mpr_relay *r) {
    uint64_t t = mpr_mono_ms();
    for (size_t i = 0; i < 64; i++)
        if (r->fwd[i].used && t >= r->fwd[i].deadline) {
            r->fwd[i].used = false;
            if (r->fwd[i].reply.fn) {
                char *e = mt_fmt("peer: the leader (system %u) did not answer in 70s; `fleet` shows whether it committed", r->fwd[i].leader);
                r->fwd[i].reply.fn(r->fwd[i].reply.ctx, false, e);
                free(e);
            }
        }
    for (size_t i = 0; i < 32; i++)
        if (r->images[i].used && t >= r->images[i].deadline) {
            r->images[i].used = false;
            waiter_done(r->images[i].w, false, mt_fmt("system %u did not send the image in 30s", r->images[i].from));
        }
    sched_view_timeouts(r, false, 0);
}

static void tick(mpr_relay *r) {
    uint64_t now = relay_now(r);
    apply(r, mfc_tick(r->core, now));
    if (mpr_mono_ms() - r->last_beat_ms >= r->tun.ping_ms) relay_beat_all(r);
    /* The leader's work: its first document, queued changes, the policies. */
    if (mfc_role_of(r->core) == MFC_LEADER) lead(r, now);
    pending_check(r);
    timeouts(r);
    group_want(r);
    if (atomic_load(&r->hello_state) == MESH_GL_HELLO && mpr_mono_ms() - r->hello_sent_ms >= HELLO_RESEND_MS) group_hello_send(r);
    send_view(r);
    if (mpr_mono_ms() - r->last_publish_ms >= 100) publish_all(r, now);
    uint32_t l;
    atomic_store(&r->pub_version, mfc_committed(r->core)->version);
    atomic_store(&r->pub_leader, mfc_leader(r->core, &l) ? l : MESH_PEER_NONE);
    atomic_store(&r->pub_role, mfc_role_code(mfc_role_of(r->core)));
    atomic_store(&r->pub_alive, mfc_alive(r->core, now));
}

/* ---- one event */

static void frame_event(mpr_relay *r, event *e) {
    uint32_t from = e->from;
    const uint8_t *b = e->frame.b + 1;
    size_t n = e->frame.n - 1;
    uint64_t now = relay_now(r);
    char *err = NULL;
    switch (e->frame.b[0]) {
    case MESH_F_HELLO: {
        uint32_t sys, ver;
        mpk_hello h;
        if (!mpk_hello_decode(b, &sys, &ver, &h, &err)) break;
        if (sys != from) err = mt_fmt("HELLO from system %u on system %u's link", sys, from);
        else if (ver != MESH_PEER_VERSION) err = mt_fmt("HELLO of peer-link version %u", ver);
        else group_hello(r, from, &h);
        break;
    }
    case MESH_F_BEAT: {
        uint32_t rn = mpw_u32_at(b, MESH_BEAT_HEAD - 4);
        uint32_t k = mpw_u32_at(b, MESH_BEAT_HEAD + rn);
        mfc_msg m;
        mpk_beat_status st;
        if (!mpk_beat_decode(b, b + MESH_BEAT_HEAD, rn, b + MESH_BEAT_HEAD + rn + 4, (size_t)k * MESH_BEAT_MOVE_BYTES, &m, &st, &err)) break;
        if (st.status == MESH_RS_FAILED && (st.status != r->seen_status[from] || !mt_eq(st.reason, r->seen_reason[from])))
            relay_log(r, true, mt_fmt("fleet: system %u failed to reconcile version %llu: %s", from, (unsigned long long)st.applied, st.reason));
        r->seen_status[from] = st.status;
        mt_set(&r->seen_reason[from], st.reason);
        mpk_beat_status_free(&r->st[from]);
        r->st[from] = st;
        apply(r, mfc_recv(r->core, from, m, now));
        break;
    }
    case MESH_F_VOTE_REQ: apply(r, mfc_recv(r->core, from, mpk_vote_req_decode(b), now)); break;
    case MESH_F_VOTE: apply(r, mfc_recv(r->core, from, mpk_vote_decode(b), now)); break;
    case MESH_F_STATE_ACK:
    case MESH_F_COMMIT: apply(r, mfc_recv(r->core, from, mpk_ack_decode(e->frame.b[0], b), now)); break;
    case MESH_F_STATE: {
        uint32_t len = mpw_u32_at(b, 8);
        char *t = mt_strndup((const char *)b + MESH_STATE_HEAD, len);
        mfd_doc d;
        if (strlen(t) != len) err = mt_strdup("F_STATE: not UTF-8");
        else if (mfd_decode(t, &d, &err)) apply(r, mfc_recv(r->core, from, (mfc_msg){.kind = MFC_STATE, .term = mpw_u64_at(b, 0), .doc = d}, now));
        free(t);
        break;
    }
    case MESH_F_CTL: {
        uint32_t id = mpw_u32_at(b, 0), len = mpw_u32_at(b, 4);
        char *line = mt_strndup((const char *)b + MESH_CTL_HEAD, len);
        replyto to = {true, from, id, {0}};
        if (mt_starts(line, "job ")) {
            /* A job request forwarded to us: the jobs thread answers it (F_CTL_REPLY), not the desired state's queue. */
            r->h.jobs_ctl(r->h.ctx, line, true, from, id, (mpr_reply){0});
            free(line);
        } else if (mfc_role_of(r->core) == MFC_LEADER) {
            r->queue_line = mt_realloc(r->queue_line, (r->nqueue + 1) * sizeof *r->queue_line);
            r->queue_to = mt_realloc(r->queue_to, (r->nqueue + 1) * sizeof *r->queue_to);
            r->queue_line[r->nqueue] = line;
            r->queue_to[r->nqueue++] = to;
        } else {
            char *lw = leader_word(r), *t = mt_fmt("peer: system %u is not the leader (%s)", r->me, lw);
            relay_reply(r, to, false, t);
            free(lw), free(t), free(line);
        }
        break;
    }
    case MESH_F_CTL_REPLY: {
        uint32_t id = mpw_u32_at(b, 0), ok = mpw_u32_at(b, 4), len = mpw_u32_at(b, 8);
        for (size_t i = 0; i < 64; i++)
            if (r->fwd[i].used && r->fwd[i].id == id) {
                r->fwd[i].used = false;
                char *t = mt_strndup((const char *)b + MESH_CTL_REPLY_HEAD, len);
                if (r->fwd[i].reply.fn) r->fwd[i].reply.fn(r->fwd[i].reply.ctx, ok != 0, t);
                free(t);
            }
        break;
    }
    case MESH_F_IMAGE_REQ: {
        char name[MESH_NAME_BYTES + 1];
        if (!mpw_name_of(b + 4, MESH_NAME_BYTES, name, &err)) break;
        uint32_t dl = mpw_u32_at(b, MESH_IMAGE_REQ_HEAD - 4);
        char *dir = mt_strndup((const char *)b + MESH_IMAGE_REQ_HEAD, dl);
        serve_image(r, from, mpw_u32_at(b, 0), name, mpw_u64_at(b, 4 + MESH_NAME_BYTES), dir);
        free(dir);
        break;
    }
    case MESH_F_IMAGE: image_reply(r, mpw_u32_at(b, 0), b, n); break;
    case MESH_F_VIEW_REQ: {
        char name[MESH_NAME_BYTES + 1];
        if (!mpw_name_of(b + 4, MESH_NAME_BYTES, name, &err)) break;
        sched_serve_view(r, from, mpw_u32_at(b, 0), name, mpw_u64_at(b, 4 + MESH_NAME_BYTES));
        break;
    }
    case MESH_F_VIEW: sched_view_chunk(r, mpw_u32_at(b, 0), b, n); break;
    default: break;
    }
    if (err) {
        drop_link(r, from, err);
        free(err);
    }
}

static void handle(mpr_relay *r, event *e) {
    switch (e->kind) {
    case EV_LINK: install(r, e); break;
    case EV_CLOSED:
        if (current(r, e->from, e->lid)) drop_link(r, e->from, e->text);
        break;
    case EV_FRAME:
        if (current(r, e->from, e->lid)) frame_event(r, e);
        break;
    case EV_CHANGE:
        change(r, e->text, e->reply);
        e->text = NULL;
        break;
    case EV_WAKE:
        relay_beat_all(r);
        group_want(r);
        break;
    case EV_FETCH_IMAGE: fetch_image(r, e->name, e->hash, e->dir, e->w); break;
    case EV_FETCH_VIEW: sched_fetch_view(r, e->name, e->hash, e->from, e->w); break;
    case EV_CTL_REPLY: relay_reply(r, (replyto){true, e->from, e->id, {0}}, e->ok, e->text); break;
    }
    event_free(e);
}

static void *relay_main(void *arg) {
    mpr_relay *r = arg;
    while (!atomic_load(&r->quit)) {
        if (atomic_load(&r->fatal)) break;
        pthread_mutex_lock(&r->ev_mu);
        if (!r->ev_n) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec += 10 * 1000000L;
            if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
            pthread_cond_timedwait(&r->ev_cv, &r->ev_mu, &ts);
        }
        event *batch = r->ev;
        size_t nb = r->ev_n;
        r->ev = NULL, r->ev_n = r->ev_cap = 0;
        pthread_mutex_unlock(&r->ev_mu);
        for (size_t i = 0; i < nb; i++) handle(r, &batch[i]);
        free(batch);
        tick(r);
    }
    return NULL;
}

/* ---- the API */

mpr_relay *mpr_start(const mcc_topology *topo, const mpk_caps *caps, const char *state_path, const mpr_host *host, char **err) {
    mpr_relay *r = mt_alloc(sizeof *r);
    r->h = *host;
    r->me = topo->me;
    r->n = topo->n;
    r->tun = topo->tun;
    r->my_caps = *caps;
    r->w = topo->tun.sched;
    mfp_persist p = {0};
    if (state_path) {
        if (!mfs_open(state_path, r->n, &r->store, &p, err)) {
            free(r);
            return NULL;
        }
        r->has_store = true;
    }
    pthread_mutex_init(&r->ev_mu, NULL);
    pthread_cond_init(&r->ev_cv, NULL);
    pthread_mutex_init(&r->links_mu, NULL);
    pthread_mutex_init(&r->group_mu, NULL);
    pthread_mutex_init(&r->send_mu, NULL);
    pthread_mutex_init(&r->tag_mu, NULL);
    pthread_mutex_init(&r->own_mu, NULL);
    board_init(&r->board);
    atomic_init(&r->quit, false);
    atomic_init(&r->fatal, false);
    atomic_init(&r->mirroring, false);
    atomic_init(&r->hello_state, MESH_GL_NONE);
    atomic_init(&r->last_sent, UINT64_MAX);
    atomic_init(&r->pub_version, 0);
    atomic_init(&r->pub_leader, MESH_PEER_NONE);
    atomic_init(&r->pub_role, 1);
    atomic_init(&r->pub_alive, 1u << topo->me);
    memset(r->tag.rank_of, 0xff, sizeof r->tag.rank_of);
    r->tag.me = 0xff;
    r->tracker = mst_new();
    r->next_id = 1;
    r->t0_ms = mpr_mono_ms();
    r->last_beat_ms = r->last_publish_ms = r->last_sched_text_ms = r->hello_sent_ms = mpr_mono_ms();
    r->h.peer_state(r->h.ctx, MPR_PEER_WAITING);
    publish_member(r, r->me, MESH_MS_UP, caps);
    if (p.committed.version > 0) {
        relay_log(r, false, mt_fmt("fleet: system %u of %u: term %llu, committed version %llu (term %llu) from --fleet-state; reconciling to it", r->me, r->n,
                                   (unsigned long long)p.term, (unsigned long long)p.committed.version, (unsigned long long)p.committed.term));
        r->h.committed(r->h.ctx, &p.committed);
    }
    uint64_t seed = (uint64_t)getpid() << 20 ^ mpr_mono_us() ^ ((uint64_t)r->me + 1) * 0x9e3779b97f4a7c15ull;
    r->core = mfc_new((mfc_config){r->me, r->n, r->tun.dead_ms, seed}, p, 1);
    const mfc_config *cfg = mfc_cfg(r->core);
    relay_log(r, false, mt_fmt("fleet: system %u of %u: %s; a majority is %u; beats every %llu ms, a member is gone after %llu ms", r->me, r->n,
                               mfc_fixed(cfg) ? "a pair: system 0 coordinates (no failover)" : "leader by election", mfc_quorum(cfg), (unsigned long long)r->tun.ping_ms,
                               (unsigned long long)r->tun.dead_ms));
    mpn_owner owner = {r, on_linked, on_link_log};
    if (topo->listen) {
        int fd = mpn_listen(topo->listen, err);
        if (fd < 0) {
            relay_log(r, true, mt_fmt("peer: %s", *err));
            mpr_stop(r);
            return NULL;
        }
        relay_log(r, false, mt_fmt("peer: system %u of %u listening on %s", r->me, r->n, topo->listen));
        r->acceptor = mpn_acceptor_start(fd, r->me, r->n, caps, (uint32_t)r->tun.dead_ms, &owner);
    }
    for (size_t i = 0; i < topo->ndial; i++) {
        relay_log(r, false, mt_fmt("peer: system %u dials system %u at %s", r->me, topo->dial_k[i], topo->dial_addr[i]));
        r->dialers[r->ndialers++] = mpn_dialer_start(topo->dial_k[i], topo->dial_addr[i], r->me, r->n, caps, (uint32_t)r->tun.dead_ms, &owner);
    }
    pthread_create(&r->sender, NULL, group_sender, r);
    pthread_create(&r->thread, NULL, relay_main, r);
    return r;
}

void mpr_stop(mpr_relay *r) {
    if (!r) return;
    atomic_store(&r->quit, true);
    mpn_acceptor_stop(r->acceptor);
    if (r->thread) pthread_join(r->thread, NULL);
    if (r->sender) pthread_join(r->sender, NULL);
    /* The links before their dialers: closing a link clears its dialer's alive flag. */
    for (uint32_t k = 0; k < MAXS; k++) free_linkrec(r->links[k]);
    for (size_t i = 0; i < r->ndialers; i++) mpn_dialer_stop(r->dialers[i]);
    for (size_t i = 0; i < r->ev_n; i++) {
        if (r->ev[i].kind == EV_LINK) close(r->ev[i].fd);
        event_free(&r->ev[i]);
    }
    free(r->ev);
    mfc_free(r->core);
    if (r->has_store) mfs_close(&r->store);
    for (uint32_t k = 0; k < MAXS; k++) {
        mpk_beat_status_free(&r->st[k]);
        free(r->seen_reason[k]);
    }
    for (size_t i = 0; i < r->nqueue; i++) free(r->queue_line[i]);
    free(r->queue_line), free(r->queue_to);
    if (r->has_pending) pending_free(&r->pending);
    for (size_t i = 0; i < r->nwaiting; i++) pending_free(&r->waiting[i]);
    free(r->waiting);
    for (size_t i = 0; i < 32; i++) {
        if (r->images[i].used) waiter_done(r->images[i].w, false, mt_strdup("the fleet thread stopped"));
        if (r->views[i].used) {
            mpv_wait_free(&r->views[i].v);
            waiter_done(r->views[i].w, false, mt_strdup("the fleet thread stopped"));
        }
    }
    ms_free(&r->paused_since);
    free(r->view_places);
    free(r->hello_why);
    mst_free(r->tracker);
    if (r->has_mig) mst_move_free(&r->mig.mv);
    for (size_t i = 0; i < r->nperf_mem; i++) free(r->perf_mem[i].name);
    free(r->perf_mem);
    for (size_t i = 0; i < r->nfail_mem; i++) free(r->fail_mem[i].name), free(r->fail_mem[i].why);
    free(r->fail_mem);
    mt_strs_free(&r->sched_log);
    free(r->group_prop);
    msp_explains_free(&r->explains);
    board_free(&r->board);
    pthread_mutex_destroy(&r->ev_mu);
    pthread_cond_destroy(&r->ev_cv);
    pthread_mutex_destroy(&r->links_mu);
    pthread_mutex_destroy(&r->group_mu);
    pthread_mutex_destroy(&r->send_mu);
    pthread_mutex_destroy(&r->tag_mu);
    pthread_mutex_destroy(&r->own_mu);
    free(r);
}

void mpr_change(mpr_relay *r, const char *line, mpr_reply reply) { post(r, (event){.kind = EV_CHANGE, .text = mt_strdup(line), .reply = reply}); }
void mpr_wake(mpr_relay *r) { post(r, (event){.kind = EV_WAKE}); }

static bool wait_for(waiter *w, uint32_t ms, char **err) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
    pthread_mutex_lock(&w->mu);
    while (!w->done)
        if (pthread_cond_timedwait(&w->cv, &w->mu, &ts) != 0) break;
    bool done = w->done, ok = w->ok;
    pthread_mutex_unlock(&w->mu);
    if (!done) {
        *err = mt_fmt("no answer in %us", ms / 1000);
        return false;
    }
    if (!ok) *err = w->err ? w->err : mt_strdup("failed");
    else free(w->err);
    return ok;
}

bool mpr_fetch_image(mpr_relay *r, const char *name, uint64_t hash, const char *dir, uint32_t wait_ms, mpr_payload *out, char **err) {
    waiter *w = mt_alloc(sizeof *w);
    pthread_mutex_init(&w->mu, NULL);
    pthread_cond_init(&w->cv, NULL);
    post(r, (event){.kind = EV_FETCH_IMAGE, .name = mt_strdup(name), .hash = hash, .dir = mt_strdup(dir), .w = w});
    bool ok = wait_for(w, wait_ms, err);
    if (ok) *out = w->payload;
    /* A late answer would write into w: it is leaked rather than freed under the relay (rare: a
     * timeout); the relay drops its slot at IMAGE_WAIT. */
    if (ok || w->done) {
        if (!ok) mpr_payload_free(&w->payload);
        pthread_mutex_destroy(&w->mu);
        pthread_cond_destroy(&w->cv);
        free(w);
    }
    return ok;
}

bool mpr_fetch_view(mpr_relay *r, const char *name, uint64_t hash, uint32_t from, uint32_t wait_ms, uint32_t *view_abi, mpw_bytes *view, char **err) {
    waiter *w = mt_alloc(sizeof *w);
    pthread_mutex_init(&w->mu, NULL);
    pthread_cond_init(&w->cv, NULL);
    post(r, (event){.kind = EV_FETCH_VIEW, .name = mt_strdup(name), .hash = hash, .from = from, .w = w});
    bool ok = wait_for(w, wait_ms, err);
    if (ok) *view_abi = w->view_abi, *view = w->view;
    if (ok || w->done) {
        if (!ok) mpw_bytes_free(&w->view);
        pthread_mutex_destroy(&w->mu);
        pthread_cond_destroy(&w->cv);
        free(w);
    }
    return ok;
}

uint64_t mpr_version(mpr_relay *r) { return atomic_load(&r->pub_version); }
bool mpr_leader(mpr_relay *r, uint32_t *leader) {
    uint32_t l = atomic_load(&r->pub_leader);
    if (l == MESH_PEER_NONE) return false;
    if (leader) *leader = l;
    return true;
}
uint32_t mpr_role(mpr_relay *r) { return atomic_load(&r->pub_role); }
uint32_t mpr_alive(mpr_relay *r) { return atomic_load(&r->pub_alive); }
