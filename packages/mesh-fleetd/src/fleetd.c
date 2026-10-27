/* The fleet member's start, its bridges and its stop (main.rs's fleet thread; peer.rs `run`'s start). */
#include "fleetd_int.h"

#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/fleet_store.h"
#include "mesh/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

mesh_fleetd *mesh_fleetd_open(mesh_shared *sh, const mcc_topology *topo, const char *state_path, bool *skip_startup, char **err) {
    *skip_startup = false;
    if (state_path) {
        /* Read (and written back, so its directory is known writable) before anything runs; the fleet
         * thread opens it again when it starts. */
        mfs_store st;
        mfp_persist p;
        if (!mfs_open(state_path, topo->n, &st, &p, err)) return NULL;
        *skip_startup = p.committed.version > 0;
        mfp_free(&p);
        mfs_close(&st);
    }
    mesh_fleetd *f = mesh_xcalloc(1, sizeof *f);
    f->sh = sh;
    f->topo = *topo;
    f->topo.listen = topo->listen ? mesh_xstrdup(topo->listen) : NULL;
    for (size_t i = 0; i < topo->ndial; i++) f->topo.dial_addr[i] = mesh_xstrdup(topo->dial_addr[i]);
    f->state_path = state_path ? mesh_xstrdup(state_path) : NULL;
    pthread_mutex_init(&f->mu, NULL);
    f->reason = mesh_xstrdup("");
    atomic_init(&f->relay, NULL);
    atomic_init(&f->starting, false);
    atomic_init(&f->stop, false);
    return f;
}

static void put_str(char *out, size_t cap, const char *s) {
    snprintf(out, cap, "%s", s ? s : "");
}

mpk_caps mesh_fleetd_caps(mesh_shared *sh, const char *gpu, const char *arch) {
    mesh_mailbox mb = sh->mb;
    mpk_caps c;
    memset(&c, 0, sizeof c);
    c.cc = mesh_mailbox_get(mb, MESH_H_CC);
    c.sms = mesh_mailbox_get(mb, MESH_H_SMS);
    c.sms_total = mesh_mailbox_get(mb, MESH_H_SMS_TOTAL);
    c.blocks_per_sm = mesh_mailbox_get(mb, MESH_H_BLOCKS_PER_SM);
    c.backend = mesh_mailbox_get(mb, MESH_H_BACKEND);
    c.max_threads = mesh_mailbox_get(mb, MESH_H_MAX_TG_THREADS);
    c.max_shared = mesh_mailbox_get(mb, MESH_H_MAX_SHARED);
    put_str(c.gpu, sizeof c.gpu, gpu);
    put_str(c.arch, sizeof c.arch, arch);
    char host[256] = "";
    gethostname(host, sizeof host - 1);
    put_str(c.host, sizeof c.host, host);
    return c;
}

mpr_relay *fd_relay(mesh_fleetd *f) {
    for (int i = 0; i < 5000; i++) {
        mpr_relay *r = atomic_load(&f->relay);
        if (r || !atomic_load(&f->starting) || atomic_load(&f->stop)) return r;
        mesh_sleep_us(1000);
    }
    return atomic_load(&f->relay);
}

mpr_relay *mesh_fleetd_relay(mesh_fleetd *f) { return atomic_load(&f->relay); }

static bool quitting(mesh_fleetd *f) { return atomic_load(&f->stop) || atomic_load(f->sh->quit); }

/* The fleet thread: the links wait until the startup loads ran (the first leader's document is what
 * it runs). A listener that cannot be bound ends it (the Rust's thread returns): this member then
 * runs alone, and changes are refused. */
static void *fleet_main(void *p) {
    mesh_fleetd *f = p;
    while (!atomic_load(&f->sh->ready) && !quitting(f)) mesh_sleep_us(10000);
    if (quitting(f)) {
        atomic_store(&f->starting, false);
        return NULL;
    }
    char *err = NULL;
    mpr_relay *r = mpr_start(&f->topo, &f->caps, f->state_path, &f->host, &err);
    if (!r) {
        mesh_error("fleet: %s", err ? err : "the fleet thread did not start");
        free(err);
        mesh_chan_close_rx(f->sh->peer);
    } else {
        atomic_store(&f->relay, r);
        mesh_info("system %u of %u: a fleet member (a lockstep program runs once its group's members HELLO)", f->topo.me, f->topo.n);
    }
    atomic_store(&f->starting, false);
    return NULL;
}

/* The `peer` channel's events from other threads: a change (control port, driver, jobs thread) and a
 * wake (the reconciler). It drains once the relay runs; until then they wait, as in the Rust's
 * channel. */
static void *bridge_main(void *p) {
    mesh_fleetd *f = p;
    mesh_shared *sh = f->sh;
    while (!quitting(f) && !atomic_load(&f->relay) && (atomic_load(&f->starting) || !atomic_load(&sh->ready))) mesh_sleep_us(10000);
    mpr_relay *r = atomic_load(&f->relay);
    if (!r) return NULL;
    while (!quitting(f)) {
        mesh_peer_event e;
        int got = mesh_chan_recv(sh->peer, &e, 100);
        if (got == MESH_CHAN_CLOSED) break;
        if (got != MESH_CHAN_OK) continue;
        switch (e.k) {
        case MESH_PEV_CHANGE:
            mpr_change(r, e.line ? e.line : "", (mpr_reply){e.reply ? fd_reply_fn : NULL, e.reply});
            e.reply = NULL;
            break;
        case MESH_PEV_WAKE: mpr_wake(r); break;
        default: mesh_error("fleet: an event of kind %d on the peer channel, which the relay receives itself; dropped", (int)e.k);
        }
        mesh_peer_event_free(&e);
    }
    return NULL;
}

typedef struct {
    mesh_fleetd *f;
    uint32_t k;
} fwd_arg;

/* The member's link queue to take from (retained), as the linked mask left it. */
static mesh_chan *queue_of(mesh_shared *sh, uint32_t k, bool bulk) {
    pthread_mutex_lock(&sh->mu);
    mesh_chan *q = bulk ? sh->bulk_out[k] : sh->link_out[k];
    if (q) mesh_chan_retain(q);
    pthread_mutex_unlock(&sh->mu);
    return q;
}

/* A frame to member k's link; a full link queue is waited on (2 s at most) while k stays linked, as the
 * Rust's writer blocks the queue behind it. */
static void forward(mesh_fleetd *f, mpr_relay *r, uint32_t k, const mesh_link_frame *x, bool bulk) {
    if (!x->n) return; /* a doorbell */
    int64_t t0 = mesh_mono_ms();
    while (!mpr_send(r, k, x->b, x->n, bulk)) {
        uint32_t linked = mesh_host_block_get_u32(&f->sh->host, MESH_S_LINKED);
        if (!(linked >> k & 1) || quitting(f) || mesh_mono_ms() - t0 > 2000) return;
        mesh_sleep_us(1000);
    }
}

/* peer.rs writer: member k's queued frames, every control frame before each bulk frame. */
static void *fwd_main(void *p) {
    fwd_arg a = *(fwd_arg *)p;
    free(p);
    mesh_fleetd *f = a.f;
    mesh_shared *sh = f->sh;
    while (!quitting(f) && !atomic_load(&f->relay) && (atomic_load(&f->starting) || !atomic_load(&sh->ready))) mesh_sleep_us(10000);
    mpr_relay *r = atomic_load(&f->relay);
    if (!r) return NULL;
    while (!quitting(f)) {
        mesh_chan *q = queue_of(sh, a.k, false), *bq = queue_of(sh, a.k, true);
        if (!q || !bq) {
            if (q) mesh_chan_release(q);
            if (bq) mesh_chan_release(bq);
            mesh_sleep_us(20000);
            continue;
        }
        mesh_link_frame x;
        for (;;) {
            while (mesh_chan_try_recv(q, &x) == MESH_CHAN_OK) {
                forward(f, r, a.k, &x, false);
                free(x.b);
            }
            if (mesh_chan_try_recv(bq, &x) == MESH_CHAN_OK) {
                forward(f, r, a.k, &x, true);
                free(x.b);
                continue;
            }
            if (quitting(f)) break;
            int got = mesh_chan_recv(q, &x, 100);
            if (got == MESH_CHAN_OK) {
                forward(f, r, a.k, &x, false);
                free(x.b);
            } else if (got == MESH_CHAN_CLOSED) break;
            /* The link ended (its queues were dropped): take the next one's. */
            pthread_mutex_lock(&sh->mu);
            bool gone = sh->link_out[a.k] != q;
            pthread_mutex_unlock(&sh->mu);
            if (gone) break;
        }
        while (mesh_chan_try_recv(q, &x) == MESH_CHAN_OK) free(x.b);
        while (mesh_chan_try_recv(bq, &x) == MESH_CHAN_OK) free(x.b);
        mesh_chan_release(q);
        mesh_chan_release(bq);
    }
    return NULL;
}

void mesh_fleetd_start(mesh_fleetd *f, const mpk_caps *caps) {
    f->caps = *caps;
    f->host = fd_host(f);
    mrc_ops ops = fd_recon_ops(f);
    f->rec = mrc_thread_start(&ops);
    fd_perf_set(f);
    atomic_store(&f->starting, true);
    f->fleet_on = pthread_create(&f->fleet_t, NULL, fleet_main, f) == 0;
    if (!f->fleet_on) atomic_store(&f->starting, false);
    f->bridge_on = pthread_create(&f->bridge_t, NULL, bridge_main, f) == 0;
    for (uint32_t k = 0; k < f->topo.n && k < MESH_MAX_SYSTEMS; k++) {
        if (k == f->topo.me) continue;
        fwd_arg *a = mesh_xmalloc(sizeof *a);
        *a = (fwd_arg){f, k};
        f->fwd_on[k] = pthread_create(&f->fwd_t[k], NULL, fwd_main, a) == 0;
        if (!f->fwd_on[k]) free(a);
    }
}

void mesh_fleetd_stop(mesh_fleetd *f) {
    if (!f) return;
    atomic_store(&f->stop, true);
    if (f->fleet_on) pthread_join(f->fleet_t, NULL);
    if (f->bridge_on) pthread_join(f->bridge_t, NULL);
    for (uint32_t k = 0; k < MESH_MAX_SYSTEMS; k++)
        if (f->fwd_on[k]) pthread_join(f->fwd_t[k], NULL);
    f->fleet_on = f->bridge_on = false;
    memset(f->fwd_on, 0, sizeof f->fwd_on);
    fd_perf_set(NULL);
    if (f->rec) mrc_thread_stop(f->rec);
    f->rec = NULL;
    mpr_relay *r = atomic_exchange(&f->relay, NULL);
    if (r) mpr_stop(r);
}

void mesh_fleetd_free(mesh_fleetd *f) {
    if (!f) return;
    mesh_fleetd_stop(f);
    free(f->topo.listen);
    for (size_t i = 0; i < f->topo.ndial; i++) free(f->topo.dial_addr[i]);
    free(f->state_path);
    free(f->reason);
    for (size_t i = 0; i < f->nmoves; i++) free(f->moves[i].name);
    free(f->moves);
    for (size_t i = 0; i < f->nfails; i++) {
        free(f->fails[i].name);
        free(f->fails[i].why);
    }
    free(f->fails);
    pthread_mutex_destroy(&f->mu);
    free(f);
}
