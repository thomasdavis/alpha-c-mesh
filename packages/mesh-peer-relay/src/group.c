/* peer.rs: the lockstep group's data plane: the reconciler's wish against the HELLO run here, the
 * HELLO exchange, pausing, and the rounds (the sender thread and the reader's F_ROUND). */
#include "relay.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/abi/exchange.h"
#include "mesh/abi/group_state.h"
#include "mesh/abi/peer_frames.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void group_set_state(mpr_relay *r, uint32_t s, const char *why) {
    atomic_store(&r->hello_state, s);
    mt_set(&r->hello_why, why);
}

uint32_t mpr_group_state(mpr_relay *r) { return atomic_load(&r->hello_state); }

void mpr_group_want(mpr_relay *r, const mpk_hello *spec, bool mirror, bool member) {
    pthread_mutex_lock(&r->group_mu);
    r->want_set = spec != NULL;
    if (spec) r->want = *spec;
    r->want_mirror = mirror;
    r->want_member = member;
    pthread_mutex_unlock(&r->group_mu);
    mpr_wake(r);
}

void group_hello_send(mpr_relay *r) {
    r->hello_sent_ms = mpr_mono_ms();
    if (!r->has_hello) return;
    mpw_bytes f = mpk_hello_frame(r->me, &r->hello);
    for (uint32_t k = 0; k < r->n; k++)
        if (k != r->me && (r->hello.mask >> k & 1)) relay_send(r, k, &f);
    mpw_bytes_free(&f);
}

/* Every other member's HELLO matches ours: run. */
static void hello_check(mpr_relay *r) {
    if (atomic_load(&r->hello_state) != MESH_GL_HELLO || !r->has_hello) return;
    mpk_hello *h = &r->hello;
    ml_list missing = {0};
    for (uint32_t k = 0; k < r->n; k++) {
        if (k == r->me || !(h->mask >> k & 1)) continue;
        if (r->has_theirs[k] && mpk_hello_eq(&r->theirs[k], h)) continue;
        if (r->has_theirs[k] && !(r->has_mismatch[k] && mpk_hello_eq(&r->mismatch[k], &r->theirs[k]))) {
            const mpk_hello *t = &r->theirs[k];
            ml_list tm = ml_members(t->mask), hm = ml_members(h->mask);
            char *a = ml_text(&tm), *b = ml_text(&hm);
            relay_log(r, false, mt_fmt("group %s: system %u is at epoch %u cycle %u members %s image %016llx; here epoch %u cycle %u members %s image %016llx (waiting)",
                                       h->group, k, t->epoch, t->cycle, a, (unsigned long long)t->hash, h->epoch, h->cycle, b, (unsigned long long)h->hash));
            free(a), free(b);
            ml_free(&tm), ml_free(&hm);
            r->has_mismatch[k] = true;
            r->mismatch[k] = *t;
        }
        ml_push(&missing, k);
    }
    if (missing.n) {
        char *l = ml_text(&missing), *why = mt_fmt("waiting for the HELLO of system(s) %s", l);
        mt_set(&r->hello_why, why);
        free(l), free(why);
        ml_free(&missing);
        return;
    }
    atomic_store(&r->mirroring, true);
    r->h.lock_feed(r->h.ctx, true);
    group_set_state(r, MESH_GL_RUNNING, "running");
    ml_list hm = ml_members(h->mask);
    char *ml = ml_text(&hm);
    uint32_t rank = 0;
    mpk_hello_rank(h, r->me, &rank);
    relay_log(r, false, mt_fmt("group %s: epoch %u cycle %u running on %u member(s) %s (rank %u here)", h->group, h->epoch, h->cycle, mpk_hello_n(h), ml, rank));
    free(ml);
    ml_free(&hm);
}

void group_hello(mpr_relay *r, uint32_t from, const mpk_hello *spec) {
    r->theirs[from] = *spec;
    r->has_theirs[from] = true;
    hello_check(r);
}

/* A new epoch or cycle is ready here: retag the rounds, empty the members' copies, HELLO them. */
static void hello_begin(mpr_relay *r, const mpk_hello *spec) {
    pthread_mutex_lock(&r->send_mu);
    atomic_store(&r->mirroring, false);
    pthread_mutex_lock(&r->tag_mu);
    uint8_t rank_of[MAXS];
    memset(rank_of, 0xff, sizeof rank_of);
    for (uint32_t rk = 0; rk < MAXS; rk++)
        if (spec->order[rk] < MAXS) rank_of[spec->order[rk]] = (uint8_t)rk;
    r->tag = (tag_t){.epoch = spec->epoch, .cycle = spec->cycle};
    memcpy(r->tag.rank_of, rank_of, sizeof rank_of);
    r->tag.me = rank_of[r->me];
    /* Only the members' rounds of this (epoch, cycle) land here from now on. */
    uint8_t zero[MESH_EX_REGION_BYTES] = {0};
    for (uint32_t rk = 0; rk < mpk_hello_n(spec); rk++)
        if (rk != r->tag.me) {
            r->h.mb_write(r->h.ctx, mesh_ex_slots(rk), zero, MESH_EX_REGION_BYTES);
            r->h.mb_publish64(r->h.ctx, mesh_ex_counter(rk), 0);
        }
    pthread_mutex_unlock(&r->tag_mu);
    pthread_mutex_unlock(&r->send_mu);
    atomic_store(&r->last_sent, UINT64_MAX);
    pthread_mutex_lock(&r->links_mu);
    for (uint32_t k = 0; k < MAXS; k++)
        if (r->links[k]) {
            pthread_mutex_lock(&r->links[k]->sent_mu);
            r->links[k]->sent_n = 0;
            pthread_mutex_unlock(&r->links[k]->sent_mu);
            atomic_store(&r->links[k]->last_round, 0);
        }
    pthread_mutex_unlock(&r->links_mu);
    pthread_mutex_lock(&r->own_mu);
    r->own_n = 0;
    pthread_mutex_unlock(&r->own_mu);
    memset(r->has_mismatch, 0, sizeof r->has_mismatch);
    ml_list hm = ml_members(spec->mask);
    char *ml = ml_text(&hm), rank[16];
    uint32_t rk;
    if (mpk_hello_rank(spec, r->me, &rk)) snprintf(rank, sizeof rank, "%u", rk);
    else strcpy(rank, "-");
    relay_log(r, false, mt_fmt("group %s: ready for epoch %u cycle %u (members %s, rank %s, image %016llx); HELLO", spec->group, spec->epoch, spec->cycle, ml, rank,
                               (unsigned long long)spec->hash));
    free(ml);
    ml_free(&hm);
    r->hello = *spec;
    r->has_hello = true;
    group_set_state(r, MESH_GL_HELLO, "waiting for every member's HELLO");
    group_hello_send(r);
    hello_check(r);
}

/* The reconciler's wish against the HELLO run here. */
void group_want(mpr_relay *r) {
    pthread_mutex_lock(&r->group_mu);
    bool set = r->want_set, mirror = r->want_mirror, member = r->want_member;
    mpk_hello want = r->want;
    pthread_mutex_unlock(&r->group_mu);
    if (set) {
        if (!r->has_hello || !mpk_hello_eq(&want, &r->hello)) hello_begin(r, &want);
        return;
    }
    /* Quiesced by the reconciler (a cycle keeps mirroring until its HELLO; an epoch change stops now). */
    if (!mirror && atomic_load(&r->mirroring)) {
        pthread_mutex_lock(&r->send_mu);
        atomic_store(&r->mirroring, false);
        pthread_mutex_unlock(&r->send_mu);
    }
    r->h.lock_feed(r->h.ctx, false);
    r->has_hello = false;
    uint32_t st = member ? MESH_GL_PAUSED : MESH_GL_NONE;
    if (atomic_load(&r->hello_state) != st) group_set_state(r, st, member ? "quiesced (the group changes)" : "not in the group's epoch");
}

void group_pause(mpr_relay *r, const char *why) {
    pthread_mutex_lock(&r->send_mu);
    atomic_store(&r->mirroring, false);
    pthread_mutex_unlock(&r->send_mu);
    r->h.lock_feed(r->h.ctx, false);
    if (r->has_hello)
        relay_log(r, true, mt_fmt("group %s: paused at epoch %u (%s); it resumes as a new epoch (reset) or re-forms", r->hello.group, r->hello.epoch, why));
    group_set_state(r, MESH_GL_PAUSED, why);
}

/* A member reached round n: our rounds <= n made the trip (rtt), and how long after our own round n its
 * round n came (the wait for it). */
static void round_stats(mpr_relay *r, linkrec *lr, uint64_t n) {
    if (n <= atomic_exchange(&lr->last_round, n)) return;
    uint64_t now = mpr_mono_us();
    double rtt = -1;
    pthread_mutex_lock(&lr->sent_mu);
    size_t i = 0;
    uint64_t last = 0;
    bool any = false;
    while (i < lr->sent_n && lr->sent_c[i] <= n) last = lr->sent_t[i++], any = true;
    memmove(lr->sent_c, lr->sent_c + i, (lr->sent_n - i) * sizeof *lr->sent_c);
    memmove(lr->sent_t, lr->sent_t + i, (lr->sent_n - i) * sizeof *lr->sent_t);
    lr->sent_n -= i;
    pthread_mutex_unlock(&lr->sent_mu);
    if (any) rtt = (double)(now - last);
    double wait = 0;
    pthread_mutex_lock(&r->own_mu);
    for (size_t j = 0; j < r->own_n; j++)
        if (r->own_c[j] >= n) {
            wait = now > r->own_t[j] ? (double)(now - r->own_t[j]) : 0;
            break;
        }
    pthread_mutex_unlock(&r->own_mu);
    if (r->h.round_stats) r->h.round_stats(r->h.ctx, lr->from, n, rtt, wait);
}

void group_round(mpr_relay *r, linkrec *lr, const uint8_t *b) {
    uint32_t e = mpw_u32_at(b, 0), c = mpw_u32_at(b, 4);
    uint64_t n = mpw_u64_at(b, 8);
    pthread_mutex_lock(&r->tag_mu);
    uint8_t rank = r->tag.rank_of[lr->from];
    /* A round of another epoch or cycle is stale (or early): dropped. */
    if (e == r->tag.epoch && c == r->tag.cycle && rank != 0xff && rank != r->tag.me) {
        r->h.mb_write(r->h.ctx, mesh_ex_slots(rank), b + MESH_ROUND_HEAD, MESH_EX_REGION_BYTES);
        r->h.mb_publish64(r->h.ctx, mesh_ex_counter(rank), n);
        pthread_mutex_unlock(&r->tag_mu);
        round_stats(r, lr, n);
        return;
    }
    pthread_mutex_unlock(&r->tag_mu);
}

static void sleep_us(long us) {
    struct timespec ts = {us / 1000000, (us % 1000000) * 1000};
    nanosleep(&ts, NULL);
}

/* Mirrors our exchange region to every other group member whenever our counter moves (~20 µs polling
 * bounds the step rate), tagged with the group's epoch and cycle. */
void *group_sender(void *arg) {
    mpr_relay *r = arg;
    while (!atomic_load(&r->quit)) {
        if (!atomic_load(&r->mirroring)) {
            sleep_us(2000);
            continue;
        }
        /* The round and its targets under the lock (a retag waits for it); the writes after it. */
        mpw_bytes f = {0};
        uint64_t n = 0, now = 0;
        linkrec *targets[MAXS];
        size_t nt = 0;
        pthread_mutex_lock(&r->send_mu);
        if (atomic_load(&r->mirroring)) {
            pthread_mutex_lock(&r->tag_mu);
            tag_t t = r->tag;
            pthread_mutex_unlock(&r->tag_mu);
            n = r->h.mb_get64(r->h.ctx, mesh_ex_counter(t.me));
            if (n != atomic_load(&r->last_sent)) {
                now = mpr_mono_us();
                uint8_t ty = MESH_F_ROUND, region[MESH_EX_REGION_BYTES], nb[8];
                mpw_put(&f, &ty, 1);
                mpw_put32(&f, t.epoch);
                mpw_put32(&f, t.cycle);
                memcpy(nb, &n, 8);
                mpw_put(&f, nb, 8);
                r->h.mb_read(r->h.ctx, mesh_ex_slots(t.me), region, sizeof region);
                mpw_put(&f, region, sizeof region);
                pthread_mutex_lock(&r->own_mu);
                if (!r->own_n || r->own_c[r->own_n - 1] < n) {
                    if (r->own_n >= 64) {
                        memmove(r->own_c, r->own_c + 1, 63 * sizeof *r->own_c);
                        memmove(r->own_t, r->own_t + 1, 63 * sizeof *r->own_t);
                        r->own_n--;
                    }
                    r->own_c[r->own_n] = n, r->own_t[r->own_n++] = now;
                }
                pthread_mutex_unlock(&r->own_mu);
                pthread_mutex_lock(&r->links_mu);
                for (uint32_t k = 0; k < MAXS; k++) {
                    linkrec *lr = r->links[k];
                    if (!lr || k == r->me || t.rank_of[k] == 0xff || !mpn_link_alive(lr->l)) continue;
                    pthread_mutex_lock(&lr->sent_mu);
                    if (lr->sent_n >= 64) {
                        memmove(lr->sent_c, lr->sent_c + 1, 63 * sizeof *lr->sent_c);
                        memmove(lr->sent_t, lr->sent_t + 1, 63 * sizeof *lr->sent_t);
                        lr->sent_n--;
                    }
                    lr->sent_c[lr->sent_n] = n, lr->sent_t[lr->sent_n++] = now;
                    pthread_mutex_unlock(&lr->sent_mu);
                    atomic_fetch_add(&lr->refs, 1);
                    targets[nt++] = lr;
                }
                pthread_mutex_unlock(&r->links_mu);
                atomic_store(&r->last_sent, n);
                pthread_mutex_unlock(&r->send_mu);
                /* The writes after the locks: a member that does not read (frozen, gone) never holds up
                 * a retag, a pause or the fleet thread. The reference keeps the link alive meanwhile. */
                for (size_t i = 0; i < nt; i++) {
                    linkrec *lr = targets[i];
                    if (!mpn_write_now(lr->l, f.b, f.n)) mpn_link_close(lr->l);
                    atomic_fetch_sub(&lr->refs, 1);
                }
                mpw_bytes_free(&f);
                sleep_us(20);
                continue;
            }
        }
        pthread_mutex_unlock(&r->send_mu);
        sleep_us(20);
    }
    return NULL;
}
