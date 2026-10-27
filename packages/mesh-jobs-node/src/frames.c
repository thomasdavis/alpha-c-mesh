/* jobs.rs's Node: frames (on_frame, answer, follow, gap). */
#include "internal.h"
#include "mesh/abi/peer_frames.h"

#include <stdlib.h>
#include <string.h>

mj_lead *mj_lead_new(uint32_t n);
void mj_lead_activate(mj_node *n, uint64_t now, uint64_t unix_ms, mj_outs *out);

mj_op_parts mj_op_parts_of(const mj_ops *ops) {
    mj_parts p = mj_parts_of(ops->v, ops->n);
    mj_op_parts r = {mxalloc(p.n * sizeof *r.v), p.n};
    for (size_t i = 0; i < p.n; i++)
        if (!mj_decode_ops(p.v[i].p, p.v[i].n, &r.v[i], NULL)) r.v[i] = (mj_ops){0};
    mj_parts_free(&p);
    return r;
}

void mj_op_parts_free(mj_op_parts *p) {
    for (size_t i = 0; i < p->n; i++) mj_ops_free(&p->v[i]);
    free(p->v);
    *p = (mj_op_parts){0};
}

/* Whether frames of leadership `lid` from `from` are to be followed (a newer one is adopted, and persisted). */
static bool follow(mj_node *n, uint32_t from, uint64_t lid, mj_outs *out) {
    if (lid < n->rep.lid) return false;
    if (lid > n->rep.lid) {
        if (from != n->cfg.me && n->lead) {
            mj_outs o = mj_node_step_down(n);
            mj_outs_append(out, &o);
        }
        mj_node_log(n, mj_log_lid(lid), out);
        n->has_lid_from = true;
        n->lid_from = from;
        n->next = 0;
        n->has_staging = false;
        return true;
    }
    /* The leadership this member followed before a restart: its leader is whoever speaks for it. */
    if (!n->has_lid_from) {
        n->has_lid_from = true;
        n->lid_from = from;
        n->next = 0;
        n->has_staging = false;
    }
    return n->lid_from == from;
}

static void gap(mj_node *n, uint32_t from, uint64_t lid, uint64_t now, mj_outs *out) {
    if ((now > n->gap_at ? now - n->gap_at : 0) < 500) return;
    n->gap_at = now;
    mj_node_send(n, from, mj_frame_ack(lid, n->next ? n->next - 1 : 0, true), out);
}

static mj_held *held_now(const mj_node *n, size_t *count) {
    size_t nh = momap_len(n->held), i = 0;
    mj_held *v = nh ? mxalloc(nh * sizeof *v) : NULL;
    uint64_t k;
    for (const mj_held *h = momap_first(n->held, &k); h; h = momap_next(n->held, &k, &k)) v[i++] = mj_held_clone(h);
    *count = nh;
    return v;
}

typedef struct {
    mj_frame *v;
    size_t n;
} frames;

static void put_parts(frames *f, uint64_t lid, uint32_t kind, uint64_t s0, uint64_t s1, const mj_ops *ops, uint32_t *seq) {
    mj_op_parts p = mj_op_parts_of(ops);
    for (size_t i = 0; i < p.n; i++) {
        f->v = mxrealloc(f->v, (f->n + 1) * sizeof *f->v);
        f->v[f->n++] = mj_frame_have(lid, kind, (*seq)++, s0, s1, p.v[i], NULL, 0);
        p.v[i] = (mj_ops){0};
    }
    mj_op_parts_free(&p);
}

void mj_node_answer(const mj_node *n, uint64_t lid, mj_frame **out, size_t *count) {
    frames f = {0};
    uint32_t seq = 0;
    mj_ops committed = {0};
    mj_table_ops(&n->rep.committed, &committed);
    put_parts(&f, lid, MESH_JOBS_HAVE_COMMITTED, n->rep.commit_lid, n->rep.commit_n, &committed, &seq);
    mj_ops_free(&committed);
    /* The entries of one leadership as one stream (the merge takes them in any order within it). */
    size_t i = 0;
    while (i < n->rep.npending) {
        uint64_t l = n->rep.pending[i].lid, last = 0;
        mj_ops ops = {0};
        while (i < n->rep.npending && n->rep.pending[i].lid == l) {
            const mj_entry *e = &n->rep.pending[i];
            last = e->n;
            if (e->replace) mj_table_ops(&e->table, &ops);
            else
                for (size_t k = 0; k < e->ops.n; k++) mj_ops_push(&ops, mj_op_clone(&e->ops.v[k]));
            i++;
        }
        put_parts(&f, lid, MESH_JOBS_HAVE_DELTA, l, last, &ops, &seq);
        mj_ops_free(&ops);
    }
    size_t nh;
    mj_held *held = held_now(n, &nh);
    uint32_t total = (uint32_t)f.n;
    f.v = mxrealloc(f.v, (f.n + 1) * sizeof *f.v);
    f.v[f.n++] = mj_frame_have(lid, MESH_JOBS_HAVE_END, total, 0, 0, (mj_ops){0}, held, nh);
    *out = f.v;
    *count = f.n;
}

static void on_have(mj_node *n, uint32_t from, mj_frame *f, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    uint32_t quorum = n->cfg.quorum;
    mj_lead *l = n->lead;
    if (!l || l->lid != f->lid) return;
    if (l->active) {
        /* A late answer: its results are welcome (the table is a union, and the working table holds every
         * committed entry already), and so are its leases for the items still queued (not leased again). */
        for (size_t i = 0; i < f->ops.n; i++) {
            mj_table_apply(&l->working, &f->ops.v[i]);
            mj_ops_push(&l->ops, f->ops.v[i]);
            f->ops.v[i] = (mj_op){0};
        }
        uint64_t dead = n->cfg.dead_ms;
        for (size_t i = 0; i < f->nheld; i++) {
            mj_held *h = &f->held[i];
            if (momap_get1(l->disp.leases, h->lease)) continue;
            mj_queue *q = momap_get1(l->disp.queues, h->job);
            if (!q) continue;
            mj_ranges items = mj_ranges_intersect(&h->items, &q->fresh), r2 = mj_ranges_intersect(&h->items, &q->retry);
            mj_ranges_union(&items, &r2);
            mj_ranges_free(&r2);
            if (mj_ranges_is_empty(&items)) {
                mj_ranges_free(&items);
                continue;
            }
            mj_ranges_subtract(&q->fresh, &items);
            mj_ranges_subtract(&q->retry, &items);
            uint64_t cnt = mj_ranges_len(&items);
            mj_outs_push(out, mj_out_log(mfmt("jobs: system %u's late answer: lease %llx of %llu item(s) adopted", from,
                                              (unsigned long long)h->lease, (unsigned long long)cnt)));
            if (h->lease > l->disp.next_lease) l->disp.next_lease = h->lease;
            bool fresh;
            mj_lease *x = momap_put1(l->disp.leases, h->lease, &fresh);
            *x = (mj_lease){h->lease, h->job, from, mstrdup(h->program), items, cnt, now, now + dead + 10000};
        }
        return;
    }
    if (l->responded >> from & 1) return;
    l->heard_mask |= (uint32_t)1 << from;
    l->heard[from] = now;
    /* A part numbered 0 starts an answer (a repeated pull's answer replaces an earlier one). */
    if (f->seq == 0 && f->kind != MESH_JOBS_HAVE_END && l->answers[from]) {
        mj_answer_free(l->answers[from]);
        free(l->answers[from]);
        l->answers[from] = NULL;
    }
    if (!l->answers[from]) {
        l->answers[from] = mxalloc(sizeof(mj_answer));
        mj_answer_init(l->answers[from]);
    }
    mj_answer *a = l->answers[from];
    if (f->kind == MESH_JOBS_HAVE_END) {
        if (a->spoilt || a->seq != f->seq) {
            mj_outs_push(out, mj_out_log(mfmt("jobs: system %u's table arrived incomplete (%u of %u parts); asking again", from, a->seq, f->seq)));
            mj_answer_free(a);
            free(a);
            l->answers[from] = NULL;
            mj_outs_push(out, mj_out_send(from, mj_frame_pull(f->lid)));
            return;
        }
        l->responded |= (uint32_t)1 << from;
        for (size_t i = 0; i < f->nheld; i++) {
            if (l->nadopt == l->capadopt) {
                l->capadopt = l->capadopt ? 2 * l->capadopt : 8;
                l->adopt = mxrealloc(l->adopt, l->capadopt * sizeof *l->adopt);
            }
            l->adopt[l->nadopt++] = (mj_adopt){from, f->held[i]};
            f->held[i] = (mj_held){0};
        }
        uint32_t cnt = 0;
        for (uint32_t k = 0; k < 32; k++) cnt += l->responded >> k & 1;
        if (cnt >= quorum) mj_lead_activate(n, now, unix_ms, out);
        return;
    }
    if (a->spoilt || a->seq != f->seq) {
        a->spoilt = true;
        return;
    }
    a->seq++;
    if (f->kind == MESH_JOBS_HAVE_COMMITTED) {
        a->commit_lid = f->src0;
        a->commit_n = f->src1;
        for (size_t i = 0; i < f->ops.n; i++) mj_table_apply(&a->committed, &f->ops.v[i]);
    } else {
        mj_ops *e = mj_answer_entry(a, f->src0, f->src1);
        mj_ops_append(e, &f->ops);
    }
}

static void on_state(mj_node *n, uint32_t from, mj_frame *f, uint64_t now, mj_outs *out) {
    uint64_t lid = f->lid;
    switch (f->kind) {
    case MESH_JOBS_ST_PART:
        /* Numbered: part 0 starts the table; any other must follow the last (review finding 9). */
        if (f->seq == 0) {
            mj_table_free(&n->staging);
            mj_table_init(&n->staging);
            for (size_t i = 0; i < f->ops.n; i++) mj_table_apply(&n->staging, &f->ops.v[i]);
            n->has_staging = true;
            n->staging_n = f->n;
            n->staging_seq = 1;
        } else if (n->has_staging && n->staging_n == f->n && n->staging_seq == f->seq) {
            for (size_t i = 0; i < f->ops.n; i++) mj_table_apply(&n->staging, &f->ops.v[i]);
            n->staging_seq++;
        } else n->has_staging = false;
        break;
    case MESH_JOBS_ST_SNAP_END:
    case MESH_JOBS_ST_REPLACE_END: {
        bool whole = n->has_staging && n->staging_n == f->n && n->staging_seq == f->seq;
        n->has_staging = false;
        if (whole && f->kind == MESH_JOBS_ST_SNAP_END && f->n == 0) {
            /* A snapshot at n = 0 would be a leader's table from before it led: never installed. */
        } else if (whole && f->kind == MESH_JOBS_ST_SNAP_END) {
            /* The committed table at the leader's commit point n; the entries of this leadership past it that
             * this member has stay (Replica::apply). */
            mj_ops ops = {0};
            mj_table_ops(&n->staging, &ops);
            mj_node_log(n, mj_log_snapshot(lid, f->n, ops), out);
            uint64_t last = mj_replica_last(&n->rep, lid);
            n->next = (last > f->n ? last : f->n) + 1;
            mj_node_send(n, from, mj_frame_ack(lid, n->next - 1, false), out);
        } else if (whole && f->kind == MESH_JOBS_ST_REPLACE_END) {
            if (f->n == n->next || (n->next == 0 && f->n == 1)) {
                mj_ops ops = {0};
                mj_table_ops(&n->staging, &ops);
                mj_node_log(n, mj_log_entry(lid, f->n, true, ops), out);
                n->next = f->n + 1;
                mj_node_send(n, from, mj_frame_ack(lid, f->n, false), out);
            } else if (f->n > n->next) gap(n, from, lid, now, out);
        } else {
            /* A part was lost: the table is not installed (a gap: the leader sends it again). */
            mj_outs_push(out, mj_out_log(mfmt("jobs: a table from system %u arrived incomplete; asking for it again", from)));
            gap(n, from, lid, now, out);
        }
        break;
    }
    case MESH_JOBS_ST_DELTA:
        if (f->n == n->next && f->n > 0) {
            mj_node_log(n, mj_log_entry(lid, f->n, false, f->ops), out);
            f->ops = (mj_ops){0};
            n->next = f->n + 1;
            mj_node_send(n, from, mj_frame_ack(lid, f->n, false), out);
        } else if (f->n > n->next) gap(n, from, lid, now, out);
        break;
    case MESH_JOBS_ST_COMMIT:
        /* A commit point past the entries here: some were lost on the way: a gap. */
        if (f->commit >= (n->next > 1 ? n->next : 1)) gap(n, from, lid, now, out);
        break;
    default: break;
    }
    /* The leader's commit point: what it covers of this leadership's entries here is committed. */
    uint64_t top = n->next ? n->next - 1 : 0;
    uint64_t c = f->commit < top ? f->commit : top;
    if (n->rep.npending && n->rep.pending[0].lid == lid && n->rep.pending[0].n <= c) mj_node_log(n, mj_log_commit(lid, c), out);
}

void mj_node_on_frame(mj_node *n, uint32_t from, mj_frame f, uint64_t now, uint64_t unix_ms, mj_outs *out) {
    uint32_t me = n->cfg.me;
    switch (f.ty) {
    case MESH_F_JOB_AVAIL:
        if (n->lead) {
            mj_lead *l = n->lead;
            if (!(from < MJ_MEMBERS && l->disp.progs.has[from] && mj_progs_eq(l->disp.progs.v[from], l->disp.progs.n[from], f.progs, f.nprogs))) {
                msb b = {0};
                if (!f.nprogs) msb_puts(&b, "no job program");
                for (size_t i = 0; i < f.nprogs; i++) msb_printf(&b, "%s%s", i ? ", " : "", f.progs[i].name);
                char *names = msb_take(&b);
                mj_outs_push(out, mj_out_log(mfmt("jobs: system %u runs %s", from, names)));
                free(names);
                if (from < MJ_MEMBERS) {
                    mj_member_progs_set(&l->disp.progs, from, f.progs, f.nprogs);
                    f.progs = NULL;
                    f.nprogs = 0;
                }
            }
        }
        break;
    case MESH_F_RESULT:
        if (mj_node_active(n)) {
            mj_node_on_results(n, from, f.items, f.nitems, now, unix_ms, out);
            f.items = NULL;
            f.nitems = 0;
        }
        break;
    case MESH_F_JOB_ACK:
        if (n->lead && n->lead->lid == f.lid && n->lead->active && from < n->cfg.n) {
            mj_lead *l = n->lead;
            /* A gap: it gets a snapshot; what it says it has is all it has (review finding 6). */
            if (f.gap) {
                l->snap_due |= (uint32_t)1 << from;
                if (f.n < l->acked[from]) l->acked[from] = f.n;
            } else if (f.n > l->acked[from]) l->acked[from] = f.n;
            mj_node_commit_check(n, out);
        }
        break;
    case MESH_F_JOB_HAVE: on_have(n, from, &f, now, unix_ms, out); break;
    case MESH_F_JOB_PULL:
        if (follow(n, from, f.lid, out)) {
            mj_frame *v;
            size_t cnt;
            mj_node_answer(n, f.lid, &v, &cnt);
            for (size_t i = 0; i < cnt; i++) mj_node_send(n, from, v[i], out);
            free(v);
        }
        break;
    case MESH_F_JOB_STATE:
        if (from != me && follow(n, from, f.lid, out)) on_state(n, from, &f, now, out);
        break;
    case MESH_F_LEASE:
        if (f.lid < n->rep.lid && from != me) break;
        {
            bool fresh;
            mj_held *h = momap_put1(n->held, f.l.lease, &fresh);
            if (!fresh) mj_held_free(h);
            *h = (mj_held){f.l.lease, f.l.job, mstrdup(f.l.program), mj_ranges_clone(&f.l.items)};
            n->avail_dirty = true;
            mj_outs_push(out, mj_out_run(f.l));
            f.l = (mj_lease_msg){0};
            f.ty = 0;
        }
        break;
    case MESH_F_LEASE_CUT:
        if (f.lid < n->rep.lid && from != me) break;
        {
            mj_held *h = momap_get1(n->held, f.lease);
            if (h) {
                mj_ranges_remove(&h->items, f.cut, UINT64_MAX);
                if (mj_ranges_is_empty(&h->items)) momap_del1(n->held, f.lease);
                n->avail_dirty = true;
            }
            mj_outs_push(out, mj_out_cut(f.lease, f.cut));
        }
        break;
    default: break;
    }
    mj_frame_free(&f);
}
