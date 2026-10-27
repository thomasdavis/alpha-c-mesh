/* jobs.rs's Node, leading: step_down, start_lead, activate, flush, snapshots, commit_check. */
#include "internal.h"
#include "mesh/jobs/lid.h"

#include <stdlib.h>
#include <string.h>

mj_lead *mj_lead_new(uint32_t n);
void mj_lead_free(mj_lead *l);
void mj_lead_activate(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out);

static unsigned ones(uint32_t x) {
    unsigned c = 0;
    for (; x; x &= x - 1) c++;
    return c;
}

mj_outs mj_node_step_down(mj_node *n) {
    mj_outs out = {0};
    mj_lead *l = n->lead;
    if (!l) return out;
    n->lead = NULL;
    for (size_t i = 0; i < l->nwaits; i++)
        mj_outs_push(&out, mj_out_answer(l->waits[i].token, false,
                                         mstrdup("jobs: the leader changed before the change was committed; `jobs` shows whether it was")));
    for (size_t i = 0; i < l->nheld; i++)
        mj_outs_push(&out, mj_out_answer(l->held[i].token, false,
                                         mstrdup("jobs: the leader changed before the submit found a member running its program; submit again")));
    /* Its entries not committed yet stay pending in the replica (a majority may have them: the next leader's pull). */
    mj_lead_free(l);
    return out;
}

void mj_node_start_lead(mj_node *n, uint64_t term, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    uint32_t me = n->cfg.me;
    uint64_t lid;
    if (!mj_lid_next(term, n->rep.lid, &lid)) {
        if (n->behind != term) {
            n->behind = term;
            mj_outs_push(out, mj_out_log(mfmt("jobs: system %u leads fleet term %llu but knows job leadership %llx of term %llu: waiting for the fleet's view", me,
                                              (unsigned long long)term, (unsigned long long)n->rep.lid, (unsigned long long)mj_lid_term(n->rep.lid))));
        }
        return;
    }
    /* Persisted before any frame of it leaves (a restart leads above it). */
    mj_node_log(n, mj_log_lid(lid), out);
    n->has_lid_from = true;
    n->lid_from = me;
    n->next = 0;
    n->has_staging = false;
    mj_outs_push(out, mj_out_log(mfmt("jobs: system %u leads the job table (term %llu, leadership %llx); pulling the members' tables", me,
                                      (unsigned long long)term, (unsigned long long)lid)));
    mj_lead *l = mj_lead_new(n->cfg.n);
    l->lid = lid;
    l->term = term;
    l->responded = (uint32_t)1 << me;
    l->answers[me] = mxalloc(sizeof(mj_answer));
    *l->answers[me] = mj_answer_of(&n->rep);
    uint64_t k;
    for (const mj_held *h = momap_first(n->held, &k); h; h = momap_next(n->held, &k, &k)) {
        if (l->nadopt == l->capadopt) {
            l->capadopt = l->capadopt ? 2 * l->capadopt : 8;
            l->adopt = mxrealloc(l->adopt, l->capadopt * sizeof *l->adopt);
        }
        l->adopt[l->nadopt++] = (mj_adopt){me, mj_held_clone(h)};
    }
    l->pull_at = now;
    n->lead = l;
    uint32_t linked = n->view.linked & ~((uint32_t)1 << me);
    for (uint32_t m = 0; m < 32; m++)
        if (linked >> m & 1) mj_outs_push(out, mj_out_send(m, mj_frame_pull(lid)));
    if (n->cfg.quorum <= 1) mj_lead_activate(n, now, unix_ms, out);
}

/* A majority's tables are merged: commit the merge as this leadership's first entry, rebuild the
 * queues, adopt the leases. */
void mj_lead_activate(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    (void)unix_ms;
    uint32_t me = n->cfg.me;
    uint64_t dead = n->cfg.dead_ms;
    uint32_t linked = n->view.linked & ~((uint32_t)1 << me);
    mj_lead *l = n->lead;
    uint64_t lid = l->lid;
    uint32_t responded = l->responded;
    mj_answer *in[MJ_MEMBERS];
    size_t nin = 0;
    for (int k = 0; k < MJ_MEMBERS; k++)
        if (l->answers[k]) in[nin++] = l->answers[k];
    mj_table working = mj_merge(in, nin);
    mj_ops ops = {0};
    mj_table_ops(&working, &ops);
    /* The first entry: the merge (a replace: the entries of the leaderships before it are in it). */
    mj_node_log(n, mj_log_entry(lid, 1, true, mj_ops_clone(&ops)), out);
    n->next = 2;
    l->active = true;
    for (int k = 0; k < MJ_MEMBERS; k++)
        if (l->answers[k]) {
            mj_answer_free(l->answers[k]);
            free(l->answers[k]);
            l->answers[k] = NULL;
        }
    mj_table_free(&l->working);
    l->working = working;
    l->n = 1;
    l->acked[me] = 1;
    mj_op_parts ps = mj_op_parts_of(&ops);
    for (size_t i = 0; i < ps.n; i++)
        for (uint32_t k = 0; k < 32; k++)
            if (linked >> k & 1) mj_outs_push(out, mj_out_send(k, mj_frame_state(lid, MESH_JOBS_ST_PART, 1, 0, (uint32_t)i, mj_ops_clone(&ps.v[i]))));
    for (uint32_t k = 0; k < 32; k++)
        if (linked >> k & 1) mj_outs_push(out, mj_out_send(k, mj_frame_state(lid, MESH_JOBS_ST_REPLACE_END, 1, 0, (uint32_t)ps.n, (mj_ops){0})));
    mj_op_parts_free(&ps);
    mj_ops_free(&ops);
    /* The queues: every active job's items left, but those in adopted leases. */
    momap_clear(l->disp.queues);
    uint64_t id;
    for (const mj_job *j = mj_table_first(&l->working, &id); j; j = mj_table_next(&l->working, &id))
        if (mj_job_active(j)) {
            mj_queue *q = momap_put1(l->disp.queues, id, NULL);
            *q = (mj_queue){mj_job_left(j), {0}};
        }
    uint64_t adopted = 0, top = 0;
    for (size_t i = 0; i < l->nadopt; i++) {
        uint32_t k = l->adopt[i].member;
        mj_held *h = &l->adopt[i].h;
        if (h->lease > top) top = h->lease;
        mj_queue *qq = momap_get1(l->disp.queues, h->job);
        if (!qq) continue;
        mj_ranges items = mj_ranges_intersect(&h->items, &qq->fresh);
        if (mj_ranges_is_empty(&items)) {
            mj_ranges_free(&items);
            continue;
        }
        mj_ranges_subtract(&qq->fresh, &items);
        mj_ranges in2 = mj_ranges_intersect(&items, &mj_table_get(&l->working, h->job)->spec.items);
        mj_ranges_free(&items);
        uint64_t size = mj_ranges_len(&in2);
        adopted += size;
        bool fresh;
        mj_lease *x = momap_put1(l->disp.leases, h->lease, &fresh);
        if (!fresh) {
            free(x->program);
            mj_ranges_free(&x->out);
        }
        *x = (mj_lease){h->lease, h->job, k, mstrdup(h->program), in2, size, now, now + dead + 10000};
    }
    for (size_t i = 0; i < l->nadopt; i++) mj_held_free(&l->adopt[i].h);
    l->nadopt = 0;
    /* Lease ids of this leadership, past any a member holds (review finding 10: never the wall clock's). */
    uint64_t base = mj_lease_base(lid);
    l->disp.next_lease = base > top ? base : top;
    size_t jobs = 0, active = 0;
    for (const mj_job *j = mj_table_first(&l->working, &id); j; j = mj_table_next(&l->working, &id)) {
        jobs++;
        active += mj_job_active(j);
    }
    mj_outs_push(out, mj_out_log(mfmt("jobs: system %u leads the job table: %u answer(s) merged, %zu job(s) (%zu active), %zu lease(s) of %llu item(s) adopted",
                                      me, ones(responded), jobs, active, momap_len(l->disp.leases), (unsigned long long)adopted)));
    mj_node_commit_check(n, out);
}

/* This tick's ops as one delta (persisted, sent to every member that is in step). */
void mj_node_flush(mj_node *n, mj_outs *out) {
    uint32_t me = n->cfg.me;
    uint32_t linked = n->view.linked & ~((uint32_t)1 << me);
    mj_lead *l = n->lead;
    if (!l || !l->active) return;
    /* Dupes counted this tick: one op per job. */
    uint64_t job;
    for (const uint64_t *cnt = momap_first(l->dupes, &job); cnt; cnt = momap_next(l->dupes, &job, &job)) {
        mj_job *j = mj_table_get(&l->working, job);
        if (!j) continue;
        uint64_t total = j->dupes > *cnt ? j->dupes : *cnt;
        if (total > j->dupes) {
            mj_op op = mj_op_dupes(job, total);
            mj_table_apply(&l->working, &op);
            mj_ops_push(&l->ops, op);
        }
    }
    if (!l->ops.n) return;
    mj_ops ops = l->ops;
    l->ops = (mj_ops){0};
    uint64_t lid = l->lid;
    mj_op_parts ps = mj_op_parts_of(&ops);
    mj_ops_free(&ops);
    for (size_t i = 0; i < ps.n; i++) {
        l->n++;
        uint64_t nn = l->n, commit = l->commit;
        uint32_t due = l->snap_due;
        l->acked[me] = nn;
        mj_node_log(n, mj_log_entry(lid, nn, false, mj_ops_clone(&ps.v[i])), out);
        n->next = nn + 1;
        uint32_t to = linked & ~due;
        for (uint32_t k = 0; k < 32; k++)
            if (to >> k & 1) mj_outs_push(out, mj_out_send(k, mj_frame_state(lid, MESH_JOBS_ST_DELTA, nn, commit, 0, mj_ops_clone(&ps.v[i]))));
    }
    mj_op_parts_free(&ps);
}

typedef struct {
    mj_frame *v;
    size_t n;
} frames;

static void push_frame(frames *f, mj_frame x) {
    f->v = mxrealloc(f->v, (f->n + 1) * sizeof *f->v);
    f->v[f->n++] = x;
}

static void table_parts(frames *f, uint64_t lid, uint64_t nn, uint64_t commit, const mj_table *t, uint32_t end_kind) {
    mj_ops ops = {0};
    mj_table_ops(t, &ops);
    mj_op_parts ps = mj_op_parts_of(&ops);
    mj_ops_free(&ops);
    for (size_t i = 0; i < ps.n; i++) {
        push_frame(f, mj_frame_state(lid, MESH_JOBS_ST_PART, nn, commit, (uint32_t)i, ps.v[i]));
        ps.v[i] = (mj_ops){0};
    }
    push_frame(f, mj_frame_state(lid, end_kind, nn, commit, (uint32_t)ps.n, (mj_ops){0}));
    mj_op_parts_free(&ps);
}

/* Members that need the committed table, then the entries after it. */
void mj_node_snapshots(mj_node *n, mj_outs *out) {
    uint32_t me = n->cfg.me;
    uint32_t linked = n->view.linked & ~((uint32_t)1 << me);
    mj_lead *l = n->lead;
    if (!l) return;
    uint32_t due = l->snap_due & linked;
    if (!due) return;
    l->snap_due &= ~due;
    uint64_t lid = l->lid, commit = l->commit;
    frames f = {0};
    /* The committed table only once this leadership committed its first entry (the merge): before, this
     * leader's committed table is its own from before it led, maybe behind the member's, which a snapshot
     * would replace. Until then the member gets the entries alone (the merge first). */
    if (commit > 0) table_parts(&f, lid, commit, commit, &n->rep.committed, MESH_JOBS_ST_SNAP_END);
    size_t entries = 0;
    for (size_t i = 0; i < n->rep.npending; i++) {
        const mj_entry *e = &n->rep.pending[i];
        if (e->lid != lid) continue;
        entries++;
        if (e->replace) table_parts(&f, lid, e->n, commit, &e->table, MESH_JOBS_ST_REPLACE_END);
        else push_frame(&f, mj_frame_state(lid, MESH_JOBS_ST_DELTA, e->n, commit, 0, mj_ops_clone(&e->ops)));
    }
    for (uint32_t k = 0; k < 32; k++) {
        if (!(due >> k & 1)) continue;
        mj_outs_push(out, mj_out_log(mfmt("jobs: system %u gets the job table (commit %llu, %zu entries after it)", k, (unsigned long long)commit, entries)));
        for (size_t i = 0; i < f.n; i++) mj_outs_push(out, mj_out_send(k, mj_frame_clone(&f.v[i])));
    }
    for (size_t i = 0; i < f.n; i++) mj_frame_free(&f.v[i]);
    free(f.v);
}

static int desc(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? 1 : x > y ? -1 : 0;
}

/* The commit point: the highest entry a majority persisted. */
void mj_node_commit_check(mj_node *n, mj_outs *out) {
    uint32_t me = n->cfg.me;
    uint32_t linked = n->view.linked & ~((uint32_t)1 << me);
    uint32_t q = n->cfg.quorum > 1 ? n->cfg.quorum : 1;
    mj_lead *l = n->lead;
    if (!l || !l->active) return;
    uint64_t *a = mxalloc(n->cfg.n * sizeof *a);
    memcpy(a, l->acked, n->cfg.n * sizeof *a);
    qsort(a, n->cfg.n, sizeof *a, desc);
    uint64_t c = a[q - 1];
    free(a);
    if (c <= l->commit) return;
    l->commit = c;
    uint64_t lid = l->lid;
    mj_wait *done = NULL;
    size_t ndone = 0, keep = 0;
    for (size_t i = 0; i < l->nwaits; i++) {
        if (l->waits[i].n <= c) {
            done = mxrealloc(done, (ndone + 1) * sizeof *done);
            done[ndone++] = l->waits[i];
        } else l->waits[keep++] = l->waits[i];
    }
    l->nwaits = keep;
    uint32_t due = l->snap_due;
    mj_node_log(n, mj_log_commit(lid, c), out);
    for (size_t i = 0; i < ndone; i++) mj_outs_push(out, mj_out_answer(done[i].token, done[i].ok, done[i].text));
    free(done);
    uint32_t to = linked & ~due;
    for (uint32_t k = 0; k < 32; k++)
        if (to >> k & 1) mj_outs_push(out, mj_out_send(k, mj_frame_state(lid, MESH_JOBS_ST_COMMIT, c, c, 0, (mj_ops){0})));
}
