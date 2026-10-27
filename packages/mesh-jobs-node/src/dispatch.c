/* jobs.rs's Node, the dispatcher: on_results, dispatch (loss, expiry, deadlines, completion, the
 * next leases, stealing), cancel. */
#include "internal.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/jobs/copies.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static void picks_free(void *v) { mj_picks_free(v); }

static uint64_t sat_sub(uint64_t a, uint64_t b) { return a > b ? a - b : 0; }

/* Rust's `f64 as u64`: saturating, NaN to 0. */
static uint64_t f2u(double x) {
    if (x != x || x <= 0.0) return 0;
    if (x >= 18446744073709551615.0) return UINT64_MAX;
    return (uint64_t)x;
}

static void free_lease(mj_lease *x) {
    free(x->program);
    mj_ranges_free(&x->out);
}

/* ---- results ----------------------------------------------------------------------------------- */

typedef struct {
    mj_job_id job;
    uint64_t s, e, lease;
} run4;

void mj_node_on_results(mj_node *n, uint32_t from, mj_item_res *items, size_t count, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    (void)unix_ms;
    uint64_t dead = n->cfg.dead_ms;
    uint32_t me = n->cfg.me;
    mj_lead *l = n->lead;
    run4 *runs = NULL;
    size_t nruns = 0;
    mj_mpmap counted = {0}; /* program -> count, member unused (0) */
    for (size_t i = 0; i < count; i++) {
        mj_item_res *r = &items[i];
        mj_job *j = mj_table_get(&l->working, r->job);
        if (!j || !mj_job_active(j)) continue;
        /* A payload result is committed only while this leader holds its blob: else it waits here, and the
         * blob is fetched from its holders (a result resent to a new leader). */
        if (r->status == MESH_JOBS_RS_OK && r->has_blob) {
            if (!mj_job_settled(j, r->item) && !mj_node_has_blob(n, r->job, &r->blob.hash)) {
                uint64_t key[5];
                mj_key5(key, r->job, &r->blob.hash);
                mj_parked_list *v = momap_put(l->parked, key, NULL);
                bool dup = false;
                for (size_t k = 0; k < v->n; k++)
                    if (v->v[k].from == from && v->v[k].r.lease == r->lease && v->v[k].r.item == r->item) dup = true;
                if (v->n < MESH_JOBS_PARKED_MAX && !dup) {
                    if (v->n == v->cap) {
                        v->cap = v->cap ? 2 * v->cap : 4;
                        v->v = mxrealloc(v->v, v->cap * sizeof *v->v);
                    }
                    v->v[v->n++] = (mj_parked){from, mj_item_res_clone(r)};
                }
                uint64_t *t = momap_get(l->fetch_at, key);
                if (!t || sat_sub(now, *t) >= MESH_JOBS_FETCH_RESEND_MS) {
                    *(uint64_t *)momap_put(l->fetch_at, key, NULL) = now > 1 ? now : 1;
                    mj_outs_push(out, mj_out_fetch_blob(r->job, r->blob.hash, r->blob.holders));
                }
                continue;
            }
            r->blob.holders |= (uint32_t)1 << me;
        }
        mj_mode mode = j->spec.mode;
        uint32_t attempts = j->spec.attempts;
        /* The program that ran it (a multi-program job's rates are per program). */
        mj_lease *x = momap_get1(l->disp.leases, r->lease);
        char *program;
        if (x) program = mstrdup(x->program);
        else {
            const char *p;
            size_t len = mj_spec_first_program(&j->spec, &p);
            program = mstrndup(p, len);
        }
        /* Was it this lease's (and still outstanding)? */
        bool mine = false;
        if (x && x->member == from) {
            bool had = mj_ranges_take(&x->out, r->item);
            if (had) {
                mj_mp *rt = mj_mpmap_get(&l->disp.rates, from, program);
                double rate = rt ? rt->rate.ips : 0.0;
                x->deadline = now + mj_lease_time(mj_ranges_len(&x->out), rate) * 3 + dead;
            }
            if (mj_ranges_is_empty(&x->out)) {
                mj_lease gone;
                momap_take(l->disp.leases, &r->lease, &gone);
                bool last = true;
                uint64_t id;
                momap_iter cur11_;
                momap_iter_init(&cur11_, l->disp.leases);
                for (const mj_lease *y = momap_iter_next1(&cur11_, &id); y; y = momap_iter_next1(&cur11_, &id))
                    if (y->member == from && strcmp(y->program, gone.program) == 0) last = false;
                mj_rate_lease_done(&mj_mpmap_put(&l->disp.rates, from, gone.program)->rate, gone.size, gone.issued, now, last);
                free_lease(&gone);
            }
            mine = had;
        }
        if (r->status != MESH_JOBS_RS_RETURNED) mj_mpmap_put(&counted, 0, program)->until++;
        free(program);
        j = mj_table_get(&l->working, r->job);
        switch (r->status) {
        case MESH_JOBS_RS_OK: {
            if (mj_job_settled(j, r->item)) {
                /* Exactly once: the first result stays; another attempt's is a duplicate (a re-send of the
                 * same is not). (An item whose run was folded away has no lease on record: not counted.) */
                uint64_t lease;
                if (mj_job_lease_of(j, r->item, &lease) && lease != r->lease) {
                    bool fresh;
                    uint64_t *d = momap_put1(l->dupes, r->job, &fresh);
                    if (fresh) *d = j->dupes;
                    (*d)++;
                }
                continue;
            }
            if (!mj_ranges_contains(&j->spec.items, r->item)) continue;
            bool keep = mode == MJ_MODE_COLLECT ? true : mode == MJ_MODE_NONZERO ? r->result != 0 : false;
            if (keep) {
                mj_res res = {r->lease, from, r->result, r->out, r->out_len, r->has_blob, r->blob};
                mj_op op = {.kind = MJ_OP_DONE, .job = r->job, .item = r->item, .res = res};
                r->out = NULL;
                r->out_len = 0;
                mj_table_apply(&l->working, &op);
                mj_ops_push(&l->ops, op);
            } else {
                mj_op op = mj_op_done_run(r->job, r->item, r->item + 1, r->lease, from);
                mj_table_apply(&l->working, &op);
                if (nruns && runs[nruns - 1].job == r->job && runs[nruns - 1].lease == r->lease && runs[nruns - 1].e == r->item)
                    runs[nruns - 1].e = r->item + 1;
                else {
                    runs = mxrealloc(runs, (nruns + 1) * sizeof *runs);
                    runs[nruns++] = (run4){r->job, r->item, r->item + 1, r->lease};
                }
            }
            break;
        }
        case MESH_JOBS_RS_FAIL: {
            if (mj_job_settled(j, r->item) || !mine) continue;
            const uint32_t *f = momap_get1(j->fails, r->item);
            uint32_t tries = (f ? *f : 0) + 1;
            char *why = mj_utf8_lossy(r->out, r->out_len);
            mj_op op = mj_op_fails(r->job, r->item, tries);
            mj_table_apply(&l->working, &op);
            mj_ops_push(&l->ops, op);
            if (tries >= attempts) {
                char *w = mfmt("system %u: %s", from, why);
                mj_op op2 = mj_op_fail(r->job, r->item, tries, w);
                free(w);
                mj_table_apply(&l->working, &op2);
                mj_ops_push(&l->ops, op2);
            } else {
                mj_queue *q = momap_get1(l->disp.queues, r->job);
                if (q) mj_ranges_add(&q->retry, r->item);
            }
            free(why);
            break;
        }
        default:
            if (mine && !mj_job_settled(j, r->item)) {
                mj_queue *q = momap_get1(l->disp.queues, r->job);
                if (q) mj_ranges_add(&q->retry, r->item);
            }
        }
    }
    for (size_t i = 0; i < nruns; i++) mj_ops_push(&l->ops, mj_op_done_run(runs[i].job, runs[i].s, runs[i].e, runs[i].lease, from));
    free(runs);
    for (size_t i = 0; i < counted.n; i++) mj_rate_count(&mj_mpmap_put(&l->disp.rates, from, counted.v[i].program)->rate, counted.v[i].until, now);
    for (size_t i = 0; i < counted.n; i++) free(counted.v[i].program);
    free(counted.v);
    for (size_t i = 0; i < count; i++) mj_item_res_free(&items[i]);
    free(items);
}

/* ---- cancel ------------------------------------------------------------------------------------ */

typedef struct {
    uint32_t to;
    mj_frame f;
} send1;

typedef struct {
    send1 *v;
    size_t n;
} sends;

static void push_send(sends *s, uint32_t to, mj_frame f) {
    s->v = mxrealloc(s->v, (s->n + 1) * sizeof *s->v);
    s->v[s->n++] = (send1){to, f};
}

static void cancel_inner(mj_lead *l, mj_job_id id, const char *why, uint32_t state, uint64_t unix_ms, sends *s) {
    mj_op a = mj_op_cancel(id, why), b = mj_op_finish(id, unix_ms, state);
    mj_table_apply(&l->working, &a);
    mj_ops_push(&l->ops, a);
    mj_table_apply(&l->working, &b);
    mj_ops_push(&l->ops, b);
    momap_del1(l->disp.queues, id);
    uint64_t lease;
    for (mj_lease *x = momap_first(l->disp.leases, &lease); x; x = momap_next(l->disp.leases, &lease, &lease)) {
        if (x->job != id) continue;
        uint32_t m = x->member;
        momap_del1(l->disp.leases, lease);
        push_send(s, m, mj_frame_cut(l->lid, lease, 0));
    }
}

/* Ends job id (cancel or deadline): its leases cut (sent as they are, to itself too), its queue dropped. */
void mj_lead_cancel(mj_lead *l, mj_job_id id, const char *why, uint32_t state, uint64_t unix_ms, mj_outs *out) {
    sends s = {0};
    cancel_inner(l, id, why, state, unix_ms, &s);
    for (size_t i = 0; i < s.n; i++) mj_outs_push(out, mj_out_send(s.v[i].to, s.v[i].f));
    free(s.v);
}

/* ---- dispatch ---------------------------------------------------------------------------------- */

static const mj_pick *pick_of(const momap *picks, mj_job_id job, uint32_t m) {
    const mj_picks *p = momap_get1(picks, job);
    return p ? mj_picks_ok(p, m) : NULL;
}

static double best_of(const momap *picks, uint32_t m) {
    double best = -1.7976931348623157e308;
    uint64_t job;
    momap_iter cur12_;
    momap_iter_init(&cur12_, picks);
    for (const mj_picks *p = momap_iter_next1(&cur12_, &job); p; p = momap_iter_next1(&cur12_, &job)) {
        for (size_t i = 0; i < p->n; i++)
            if (p->v[i].member == m) {
                if (p->v[i].ok) best = fmax(best, p->v[i].score.total);
                break;
            }
    }
    return best;
}

/* Rust's f64::total_cmp, for the orderings the dispatcher sorts by. */
static int total_cmp(double a, double b) {
    int64_t x, y;
    memcpy(&x, &a, 8);
    memcpy(&y, &b, 8);
    x ^= (int64_t)((uint64_t)(x >> 63) >> 1);
    y ^= (int64_t)((uint64_t)(y >> 63) >> 1);
    return x < y ? -1 : x > y ? 1 : 0;
}

void mj_node_dispatch(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    uint32_t me = n->cfg.me;
    uint32_t alive = n->view.alive | (uint32_t)1 << me;
    uint64_t dead = n->cfg.dead_ms;
    sends s = {0};
    mj_lead *l = n->lead;
    uint64_t lid = l->lid;
    uint64_t id;
    /* A member lost: its leases go back (no try counted: the member's fault, not the item's). */
    for (mj_lease *x = momap_first(l->disp.leases, &id); x; x = momap_next(l->disp.leases, &id, &id)) {
        if (alive >> x->member & 1) continue;
        mj_lease g;
        momap_take(l->disp.leases, &id, &g);
        char *w = mj_word(g.job);
        mj_outs_push(out, mj_out_log(mfmt("jobs: system %u lost: lease %llx of job %s: %llu item(s) re-queued", g.member, (unsigned long long)id, w,
                                          (unsigned long long)mj_ranges_len(&g.out))));
        free(w);
        mj_queue *q = momap_get1(l->disp.queues, g.job);
        if (q) mj_ranges_union(&q->retry, &g.out);
        free_lease(&g);
    }
    for (uint32_t k = 0; k < MJ_MEMBERS; k++)
        if (l->disp.progs.has[k] && !(alive >> k & 1)) mj_member_progs_remove(&l->disp.progs, k);
    /* Expired: a try counted for each item (a hanging item fails after `attempts`). */
    for (mj_lease *x = momap_first(l->disp.leases, &id); x; x = momap_next(l->disp.leases, &id, &id)) {
        if (!(now > x->deadline)) continue;
        mj_lease g;
        momap_take(l->disp.leases, &id, &g);
        uint64_t hold = MESH_JOBS_HOLDOFF_MS > 4 * dead ? MESH_JOBS_HOLDOFF_MS : 4 * dead;
        char *w = mj_word(g.job);
        mj_outs_push(out, mj_out_log(mfmt("jobs: lease %llx of job %s on system %u expired: %llu item(s) re-queued; no new %s lease there for %llu s",
                                          (unsigned long long)id, w, g.member, (unsigned long long)mj_ranges_len(&g.out), g.program,
                                          (unsigned long long)(hold / 1000))));
        free(w);
        mj_mpmap_put(&l->disp.holdoff, g.member, g.program)->until = now + hold;
        push_send(&s, g.member, mj_frame_cut(lid, id, 0));
        mj_job *j = mj_table_get(&l->working, g.job);
        if (!j) {
            free_lease(&g);
            continue;
        }
        uint32_t attempts = j->spec.attempts;
        if (mj_ranges_len(&g.out) > MESH_JOBS_EXPIRY_TRIES_MAX) {
            /* A huge lease (an endless job's): back to the queue whole, no tries counted per item. */
            mj_queue *q = momap_get1(l->disp.queues, g.job);
            if (q) mj_ranges_union(&q->retry, &g.out);
            free_lease(&g);
            continue;
        }
        for (size_t r = 0; r < g.out.n; r++)
            for (uint64_t item = g.out.v[r][0]; item < g.out.v[r][1]; item++) {
                const uint32_t *f = momap_get1(mj_table_get(&l->working, g.job)->fails, item);
                uint32_t tries = (f ? *f : 0) + 1;
                mj_op a = mj_op_fails(g.job, item, tries);
                mj_table_apply(&l->working, &a);
                mj_ops_push(&l->ops, a);
                if (tries >= attempts) {
                    char *why = mfmt("lease expired on system %u", g.member);
                    mj_op b = mj_op_fail(g.job, item, tries, why);
                    free(why);
                    mj_table_apply(&l->working, &b);
                    mj_ops_push(&l->ops, b);
                } else {
                    mj_queue *q = momap_get1(l->disp.queues, g.job);
                    if (q) mj_ranges_add(&q->retry, item);
                }
            }
        free_lease(&g);
    }
    /* Deadlines. */
    for (mj_job *j = mj_table_first(&l->working, &id); j; j = mj_table_next(&l->working, &id)) {
        if (!(mj_job_active(j) && j->spec.deadline_ms > 0 && unix_ms > j->spec.deadline_ms)) continue;
        char *w = mj_word(id);
        mj_outs_push(out, mj_out_log(mfmt("jobs: job %s passed its deadline", w)));
        free(w);
        cancel_inner(l, id, "deadline", MESH_JOBS_FIN_DEADLINE, unix_ms, &s);
    }
    /* Complete: nothing queued, nothing leased, every item done or failed. */
    for (mj_job *j = mj_table_first(&l->working, &id); j; j = mj_table_next(&l->working, &id)) {
        if (!(mj_job_active(j) && mj_job_complete(j))) continue;
        bool leased = false;
        uint64_t lk;
        momap_iter cur13_;
        momap_iter_init(&cur13_, l->disp.leases);
        for (const mj_lease *x = momap_iter_next1(&cur13_, &lk); x; x = momap_iter_next1(&cur13_, &lk))
            if (x->job == id) leased = true;
        if (leased) continue;
        mj_op op = mj_op_finish(id, unix_ms, MESH_JOBS_FIN_DONE);
        mj_table_apply(&l->working, &op);
        mj_ops_push(&l->ops, op);
        momap_del1(l->disp.queues, id);
        char *w = mj_word(id);
        mj_outs_push(out, mj_out_log(mfmt("jobs: job %s done", w)));
        free(w);
    }
    /* Who may take each job's leases (F4's filter and score), best first. */
    momap *picks = momap_new(1, sizeof(mj_picks), picks_free, NULL);
    for (mj_job *j = mj_table_first(&l->working, &id); j; j = mj_table_next(&l->working, &id))
        if (mj_job_active(j)) *(mj_picks *)momap_put1(picks, id, NULL) = mj_select(&j->spec, alive, &l->disp.progs, n->sched);
    /* The next leases: each member that runs a job program and has less than half a lease left, the
     * best-scored first (a short queue goes to the best members). */
    uint32_t members[MJ_MEMBERS];
    size_t nm = 0;
    for (uint32_t k = 0; k < MJ_MEMBERS; k++)
        if (l->disp.progs.has[k] && (alive >> k & 1)) members[nm++] = k;
    double best[MJ_MEMBERS];
    for (size_t i = 0; i < nm; i++) best[i] = best_of(picks, members[i]);
    for (size_t i = 1; i < nm; i++) { /* stable: best descending, then id */
        uint32_t m = members[i];
        double b = best[i];
        size_t j = i;
        while (j > 0) {
            int c = total_cmp(b, best[j - 1]);
            if (c > 0 || (c == 0 && m < members[j - 1])) {
                members[j] = members[j - 1];
                best[j] = best[j - 1];
                j--;
            } else break;
        }
        members[j] = m;
        best[j] = b;
    }
    for (size_t mi = 0; mi < nm; mi++) {
        uint32_t m = members[mi];
        size_t np = l->disp.progs.n[m];
        mj_prog *progs = mj_progs_clone(l->disp.progs.v[m], np);
        for (size_t pi = 0; pi < np; pi++) {
            mj_prog *p = &progs[pi];
            mj_mp *h = mj_mpmap_get(&l->disp.holdoff, m, p->name);
            if (h && now < h->until) continue;
            uint64_t outstanding = 0;
            size_t held = 0;
            uint64_t lk;
            momap_iter cur14_;
            momap_iter_init(&cur14_, l->disp.leases);
            for (const mj_lease *x = momap_iter_next1(&cur14_, &lk); x; x = momap_iter_next1(&cur14_, &lk))
                if (x->member == m && strcmp(x->program, p->name) == 0) {
                    outstanding += mj_ranges_len(&x->out);
                    held++;
                }
            /* Pipelined: the running lease and the next, never more (a queue of leases would expire at its back). */
            if (held >= 2) continue;
            mj_mp *rt = mj_mpmap_get(&l->disp.rates, m, p->name);
            double rate = rt ? rt->rate.ips : 0.0;
            uint64_t slow = rt ? rt->rate.target : 0;
            /* At least two batches of tickets in flight, and LEASE_MS at the measured rate or what slow start
             * reached, whichever is more. Slow start never runs past 4 x LEASE_MS at the measured rate. */
            uint64_t floor_ = 2 * (uint64_t)p->cap > 2 ? 2 * (uint64_t)p->cap : 2;
            if (rate > 0.0) {
                uint64_t cap4 = f2u(4.0 * rate * (double)MESH_JOBS_LEASE_MS / 1000.0);
                if (cap4 < slow) slow = cap4;
            }
            uint64_t at_rate = f2u(rate * (double)MESH_JOBS_LEASE_MS / 1000.0);
            if (at_rate > MESH_JOBS_LEASE_MAX) at_rate = MESH_JOBS_LEASE_MAX;
            uint64_t target = floor_ > slow ? floor_ : slow;
            if (at_rate > target) target = at_rate;
            if (outstanding * 2 > target) continue;
            /* The job: highest priority first, then the oldest, with items to lease. */
            mj_job_id chosen = 0;
            bool found = false;
            int64_t best_prio = 0;
            for (mj_job *j = mj_table_first(&l->working, &id); j; j = mj_table_next(&l->working, &id)) {
                if (!mj_job_active(j) || !mj_spec_runs(&j->spec, p->name)) continue;
                const mj_queue *q = momap_get1(l->disp.queues, id);
                if (!q || (mj_ranges_is_empty(&q->fresh) && mj_ranges_is_empty(&q->retry))) continue;
                if (!pick_of(picks, id, m)) continue;
                int64_t prio = -(int64_t)j->spec.priority;
                if (!found || prio < best_prio) {
                    found = true;
                    best_prio = prio;
                    chosen = id;
                }
            }
            if (found) {
                mj_job *j = mj_table_get(&l->working, chosen);
                const mj_picks *pk = momap_get1(picks, chosen);
                uint64_t k = 0;
                for (size_t i = 0; i < pk->n; i++) k += pk->v[i].ok;
                mj_queue *q = momap_get1(l->disp.queues, chosen);
                uint64_t a = mj_ranges_len(&q->fresh), b = mj_ranges_len(&q->retry);
                uint64_t left = a > UINT64_MAX - b ? UINT64_MAX : a + b;
                const mj_pick *pp = pick_of(picks, chosen, m);
                uint64_t shrunk = pp && pp->busy ? (floor_ < target ? floor_ : target) : target;
                uint64_t d = 2 * (k > 1 ? k : 1);
                uint64_t share = left / d + (left % d != 0);
                if (share < 1) share = 1;
                uint64_t size = shrunk < share ? shrunk : share;
                if (size < 1) size = 1;
                /* Re-queued items first, then fresh ones; never an item done meanwhile. */
                mj_ranges items = mj_ranges_take_front(&q->retry, size);
                if (mj_ranges_len(&items) < size) {
                    mj_ranges more = mj_ranges_take_front(&q->fresh, size - mj_ranges_len(&items));
                    mj_ranges_union(&items, &more);
                    mj_ranges_free(&more);
                }
                mj_ranges_subtract(&items, &j->done);
                uint64_t fx;
                momap_iter cur15_;
                momap_iter_init(&cur15_, j->failed);
                for (const void *v = momap_iter_next1(&cur15_, &fx); v; v = momap_iter_next1(&cur15_, &fx)) mj_ranges_take(&items, fx);
                if (mj_ranges_is_empty(&items)) {
                    mj_ranges_free(&items);
                    continue;
                }
                if (outstanding == 0) mj_rate_busy(&mj_mpmap_put(&l->disp.rates, m, p->name)->rate, now);
                l->disp.next_lease++;
                uint64_t lease = l->disp.next_lease;
                uint64_t cnt = mj_ranges_len(&items);
                /* Its deadline counts the lease queued before it there. */
                mj_lease *x = momap_put1(l->disp.leases, lease, NULL);
                *x = (mj_lease){lease, chosen, m, mstrdup(p->name), mj_ranges_clone(&items), cnt, now,
                                now + 3 * mj_lease_time(cnt + outstanding, rate) + dead};
                mj_lease_msg lm = {lease, chosen, mstrdup(p->name), j->spec.op, mj_setup_clone(j->spec.setup, j->spec.nsetup), j->spec.nsetup, items};
                push_send(&s, m, mj_frame_lease(lid, lm));
            } else if (outstanding == 0) {
                /* Steal: the tail of the lease of this program with the most time left (a lease on a member no
                 * longer eligible, cordoned or busy, first), for a job this member may run. */
                double mine_rate = rate;
                bool have_v = false, vgone = false;
                uint64_t vid = 0;
                double vsecs = 0, vr = 0;
                momap_iter cur16_;
                momap_iter_init(&cur16_, l->disp.leases);
                for (const mj_lease *x = momap_iter_next1(&cur16_, &lk); x; x = momap_iter_next1(&cur16_, &lk)) {
                    if (x->member == m || mj_ranges_len(&x->out) < 2) continue;
                    const mj_job *xj = mj_table_get(&l->working, x->job);
                    if (!xj || !mj_spec_runs(&xj->spec, p->name)) continue;
                    const mj_pick *mp = pick_of(picks, x->job, m);
                    if (!mp || mp->busy) continue;
                    /* Its member's rate, or this lease's own progress if that is slower. */
                    mj_mp *xr = mj_mpmap_get(&l->disp.rates, x->member, x->program);
                    double r = xr ? xr->rate.ips : 0.0;
                    uint64_t dt = sat_sub(now, x->issued);
                    double lr = dt >= 100 ? (double)(x->size - mj_ranges_len(&x->out)) * 1000.0 / (double)dt : r;
                    r = r > 0.0 ? fmin(r, lr) : lr;
                    bool gone = pick_of(picks, x->job, x->member) == NULL;
                    double secs = (double)mj_ranges_len(&x->out) / fmax(r, 1e-3);
                    /* max_by: the last of equals wins. */
                    if (!have_v || (gone != vgone ? gone > vgone : total_cmp(secs, vsecs) >= 0)) {
                        have_v = true;
                        vid = lk;
                        vsecs = secs;
                        vr = r;
                        vgone = gone;
                    }
                }
                if (have_v) {
                    if (!vgone && vsecs * 1000.0 < 2.0 * (double)MESH_JOBS_LEASE_MS) continue;
                    mj_lease *x = momap_get1(l->disp.leases, vid);
                    uint64_t cnt = mj_ranges_len(&x->out);
                    double share = mine_rate > 0.0 && vr > 0.0 ? vr / (vr + mine_rate) : 0.5;
                    uint64_t keep = f2u(ceil((double)cnt * share));
                    if (keep < 1) keep = 1;
                    if (keep > cnt - 1) keep = cnt - 1;
                    uint64_t cut;
                    if (!mj_ranges_nth(&x->out, keep, &cut)) continue;
                    mj_ranges moved = mj_ranges_clone(&x->out);
                    mj_ranges_remove(&moved, 0, cut);
                    mj_ranges_remove(&x->out, cut, UINT64_MAX);
                    char *w = mj_word(x->job);
                    mj_outs_push(out, mj_out_log(mfmt("jobs: system %u idle: steals %llu item(s) of lease %llx (job %s) from system %u%s", m,
                                                      (unsigned long long)mj_ranges_len(&moved), (unsigned long long)vid, w, x->member,
                                                      vgone ? " (no longer eligible there)" : "")));
                    free(w);
                    push_send(&s, x->member, mj_frame_cut(lid, vid, cut));
                    mj_queue *q = momap_get1(l->disp.queues, x->job);
                    if (q) mj_ranges_union(&q->retry, &moved);
                    mj_ranges_free(&moved);
                }
            }
        }
        mj_progs_free(progs, np);
    }
    momap_free(l->disp.picks);
    l->disp.picks = picks;
    for (size_t i = 0; i < s.n; i++) mj_node_send(n, s.v[i].to, s.v[i].f, out);
    free(s.v);
}
