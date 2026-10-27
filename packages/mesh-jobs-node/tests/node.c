/* jobs.rs's direct node tests: review findings 6 and 9, no snapshot before the merge commits, an
 * answer packs its entries, and nothing leaves without its persist. (The fleet tests, and the node
 * traced output for output against the Rust, are in @ajax/mesh-jobs-sim.) */
#include "mesh/jobs/node.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

static mj_spec spec(mj_job_id id, uint64_t n) {
    mj_setup su = {6, 0};
    return (mj_spec){.id = id, .program = mstrdup("p"), .op = 254, .items = mj_ranges_of(0, n), .attempts = 3, .mode = MJ_MODE_COLLECT,
                     .setup = mj_setup_clone(&su, 1), .nsetup = 1, .submitted_ms = 5, .members = 5};
}

static mj_ops one(mj_op o) {
    mj_ops v = {0};
    mj_ops_push(&v, o);
    return v;
}

static mj_ops submit(mj_job_id id, uint64_t n) {
    mj_spec s = spec(id, n);
    mj_ops v = one(mj_op_submit(&s));
    mj_spec_free(&s);
    return v;
}

static mj_cfg cfg(uint32_t me) { return (mj_cfg){me, 3, 2, 3000}; }

/* Some Send(to, frame) in outs with type ty (and, for ACK, n and gap). */
static bool sent(const mj_outs *o, uint32_t to, uint8_t ty, int64_t n, int gap) {
    for (size_t i = 0; i < o->n; i++) {
        const mj_out *x = &o->v[i];
        if (x->kind != MJ_OUT_SEND || x->to != to || x->frame.ty != ty) continue;
        if (n >= 0 && x->frame.n != (uint64_t)n) continue;
        if (gap >= 0 && x->frame.gap != (gap != 0)) continue;
        return true;
    }
    return false;
}

static bool sent_state(const mj_outs *o, uint32_t to, uint32_t kind, int64_t n) {
    for (size_t i = 0; i < o->n; i++) {
        const mj_out *x = &o->v[i];
        if (x->kind == MJ_OUT_SEND && x->to == to && x->frame.ty == MESH_F_JOB_STATE && x->frame.kind == kind && (n < 0 || x->frame.n == (uint64_t)n))
            return true;
    }
    return false;
}

static void merge_into(mj_outs *a, mj_outs b) { mj_outs_append(a, &b); }

/* Review finding 6, the node half: the snapshot's acknowledgement says 10, not 8. */
static void snapshot_ack(void) {
    uint64_t l = (uint64_t)3 << 40;
    mj_replica r;
    mj_replica_init(&r);
    mj_log seq[] = {mj_log_lid(l), mj_log_snapshot(l, 8, (mj_ops){0}), mj_log_entry(l, 9, false, one(mj_op_fails(1, 9, 1))),
                    mj_log_entry(l, 10, false, one(mj_op_fails(1, 10, 1)))};
    for (int i = 0; i < 4; i++) {
        mj_replica_apply(&r, &seq[i]);
        mj_log_free(&seq[i]);
    }
    mj_log s8 = mj_log_snapshot(l, 8, submit(1, 100));
    mj_replica_apply(&r, &s8);
    mj_log_free(&s8);
    mj_log s9 = mj_log_snapshot(l, 9, submit(1, 100));
    mj_replica_apply(&r, &s9);
    mj_log_free(&s9);
    mj_node *f = mj_node_new(cfg(1), mj_replica_clone(&r));
    mj_outs o = mj_node_recv(f, 0, mj_frame_state(l, MESH_JOBS_ST_PART, 9, 9, 0, submit(1, 100)), 1, 1);
    merge_into(&o, mj_node_recv(f, 0, mj_frame_state(l, MESH_JOBS_ST_SNAP_END, 9, 9, 1, (mj_ops){0}), 2, 2));
    CHECK(sent(&o, 0, MESH_F_JOB_ACK, 10, 0));
    CHECK_U(mj_replica_last(&f->rep, l), 10);
    mj_outs_free(&o);
    mj_node_free(f);
    mj_replica_free(&r);
}

static mj_frame part(uint64_t l, uint32_t seq, uint64_t item) {
    if (item == 0) return mj_frame_state(l, MESH_JOBS_ST_PART, 1, 0, seq, submit(1, 100));
    mj_res res = {.lease = 1, .member = 0, .result = 1};
    return mj_frame_state(l, MESH_JOBS_ST_PART, 1, 0, seq, one(mj_op_done(1, item, &res)));
}

static mj_frame have(uint64_t lid, uint32_t kind, uint32_t seq, uint64_t src0) { return mj_frame_have(lid, kind, seq, src0, 1, (mj_ops){0}, NULL, 0); }

/* Review finding 9: a table in parts with a part lost is not installed; a pull's answer with a part
 * lost is asked again. */
static void part_lost(void) {
    uint64_t l = (uint64_t)5 << 40;
    mj_replica r;
    mj_replica_init(&r);
    mj_node *f = mj_node_new(cfg(1), r);
    mj_outs o = {0};
    merge_into(&o, mj_node_recv(f, 0, part(l, 0, 0), 1000, 1));
    merge_into(&o, mj_node_recv(f, 0, part(l, 2, 3), 1000, 1));
    merge_into(&o, mj_node_recv(f, 0, mj_frame_state(l, MESH_JOBS_ST_REPLACE_END, 1, 0, 3, (mj_ops){0}), 1000, 1));
    CHECK(f->rep.npending == 0 && sent(&o, 0, MESH_F_JOB_ACK, -1, 1));
    mj_outs_free(&o);
    /* Sent again whole: installed, without the earlier part 2's leftovers. */
    merge_into(&o, mj_node_recv(f, 0, part(l, 0, 0), 2000, 2));
    merge_into(&o, mj_node_recv(f, 0, part(l, 1, 2), 2000, 2));
    mj_outs_free(&o);
    o = mj_node_recv(f, 0, mj_frame_state(l, MESH_JOBS_ST_REPLACE_END, 1, 0, 2, (mj_ops){0}), 2000, 2);
    CHECK(sent(&o, 0, MESH_F_JOB_ACK, 1, 0));
    mj_outs_free(&o);
    CHECK(f->rep.npending == 1 && f->rep.pending[0].replace && f->rep.pending[0].n == 1);
    char *w = mj_ranges_word_new(&mj_table_get(&f->rep.pending[0].table, 1)->done);
    CHECK_S(w, "2-3");
    free(w);
    mj_node_free(f);
    /* A new leader's pull: an answer with a part lost is asked again, not merged. */
    mj_replica r2;
    mj_replica_init(&r2);
    mj_node *ld = mj_node_new((mj_cfg){0, 3, 2, 3000}, r2);
    o = mj_node_set_view(ld, (mj_view){6, true, 0, 0x7, 0x7}, 1, 1);
    uint64_t lid = mj_node_lid(ld);
    CHECK(sent(&o, 1, MESH_F_JOB_PULL, -1, -1));
    mj_outs_free(&o);
    merge_into(&o, mj_node_recv(ld, 1, have(lid, MESH_JOBS_HAVE_COMMITTED, 0, l), 2, 2));
    merge_into(&o, mj_node_recv(ld, 1, have(lid, MESH_JOBS_HAVE_END, 2, l), 3, 3));
    CHECK(!mj_node_active(ld) && sent(&o, 1, MESH_F_JOB_PULL, -1, -1));
    mj_outs_free(&o);
    merge_into(&o, mj_node_recv(ld, 1, have(lid, MESH_JOBS_HAVE_COMMITTED, 0, l), 4, 4));
    merge_into(&o, mj_node_recv(ld, 1, have(lid, MESH_JOBS_HAVE_DELTA, 1, l), 4, 4));
    merge_into(&o, mj_node_recv(ld, 1, have(lid, MESH_JOBS_HAVE_END, 2, l), 5, 5));
    CHECK(mj_node_active(ld));
    mj_outs_free(&o);
    mj_node_free(ld);
}

/* A new leader whose merge (its first entry) is not committed yet sends no snapshot. */
static void no_snapshot_before_merge(void) {
    mj_replica r;
    mj_replica_init(&r);
    mj_node *ld = mj_node_new((mj_cfg){0, 3, 2, 3000}, r);
    mj_outs o = mj_node_set_view(ld, (mj_view){6, true, 0, 0x3, 0x3}, 1, 1);
    mj_outs_free(&o);
    uint64_t lid = mj_node_lid(ld);
    o = mj_node_recv(ld, 1, mj_frame_have(lid, MESH_JOBS_HAVE_COMMITTED, 0, 0, 0, (mj_ops){0}, NULL, 0), 2, 2);
    mj_outs_free(&o);
    o = mj_node_recv(ld, 1, mj_frame_have(lid, MESH_JOBS_HAVE_END, 1, 0, 0, (mj_ops){0}, NULL, 0), 2, 2);
    mj_outs_free(&o);
    CHECK(mj_node_active(ld) && mj_node_commit_n(ld) == 0);
    /* System 2 links: it is due a snapshot, but gets only the merge entry while nothing is committed. */
    o = mj_node_set_view(ld, (mj_view){6, true, 0, 0x7, 0x7}, 3, 3);
    merge_into(&o, mj_node_tick(ld, 4, 4));
    CHECK(!sent_state(&o, 2, MESH_JOBS_ST_SNAP_END, -1));
    CHECK(sent_state(&o, 2, MESH_JOBS_ST_REPLACE_END, 1));
    mj_outs_free(&o);
    /* A follower never installs one at n = 0. */
    mj_replica r2;
    mj_replica_init(&r2);
    mj_node *f = mj_node_new((mj_cfg){2, 3, 2, 3000}, r2);
    mj_spec s = spec(1, 10);
    mj_op sub = mj_op_submit(&s);
    mj_table_apply(&f->rep.committed, &sub);
    mj_op_free(&sub);
    mj_spec_free(&s);
    o = mj_node_recv(f, 0, mj_frame_state(lid, MESH_JOBS_ST_PART, 0, 0, 0, (mj_ops){0}), 5, 5);
    merge_into(&o, mj_node_recv(f, 0, mj_frame_state(lid, MESH_JOBS_ST_SNAP_END, 0, 0, 1, (mj_ops){0}), 5, 5));
    CHECK(mj_table_get(&f->rep.committed, 1) != NULL);
    mj_outs_free(&o);
    mj_node_free(f);
    mj_node_free(ld);
}

/* A pull's answer packs a leadership's entries into parts by size, not one part per entry; the merge
 * takes it whole. */
static void answer_packs(void) {
    uint64_t l = (uint64_t)4 << 40;
    mj_replica r;
    mj_replica_init(&r);
    mj_log snap = mj_log_snapshot(l, 1, submit(1, 100000));
    mj_replica_apply(&r, &snap);
    mj_log_free(&snap);
    for (uint64_t n = 2; n < 3002; n++) {
        mj_res res = {.lease = 9, .member = 1, .result = (uint32_t)n};
        mj_log e = mj_log_entry(l, n, false, one(mj_op_done(1, n, &res)));
        mj_replica_apply(&r, &e);
        mj_log_free(&e);
    }
    mj_node *f = mj_node_new(cfg(1), r);
    mj_frame *a;
    size_t na;
    mj_node_answer(f, (uint64_t)5 << 40, &a, &na);
    CHECK(na <= 4);
    mj_answer ans;
    mj_answer_init(&ans);
    for (size_t i = 0; i < na; i++) {
        if (a[i].kind == MESH_JOBS_HAVE_COMMITTED)
            for (size_t k = 0; k < a[i].ops.n; k++) mj_table_apply(&ans.committed, &a[i].ops.v[k]);
        else if (a[i].kind == MESH_JOBS_HAVE_DELTA) {
            mj_ops *e = mj_answer_entry(&ans, a[i].src0, a[i].src1);
            mj_ops c = mj_ops_clone(&a[i].ops);
            mj_ops_append(e, &c);
        }
        mj_frame_free(&a[i]);
    }
    free(a);
    mj_answer *in[1] = {&ans};
    mj_table t = mj_merge(in, 1);
    CHECK_U(momap_len(mj_table_get(&t, 1)->results), 3000);
    mj_table_free(&t);
    mj_answer_free(&ans);
    mj_node_free(f);
}

typedef struct {
    int appends, fail_append;
    bool fail_sync, synced;
} disk;
static bool app(void *c, const mj_log *l, char **err) {
    disk *d = c;
    (void)l;
    d->appends++;
    d->synced = false;
    if (d->appends == d->fail_append) {
        *err = mstrdup("ENOSPC");
        return false;
    }
    return true;
}
static bool syn(void *c, char **err) {
    disk *d = c;
    if (d->fail_sync) {
        *err = mstrdup("EIO");
        return false;
    }
    d->synced = true;
    return true;
}

static size_t acks(const mj_outs *o) {
    size_t n = 0;
    for (size_t i = 0; i < o->n; i++) n += o->v[i].kind == MJ_OUT_SEND && o->v[i].frame.ty == MESH_F_JOB_ACK;
    return n;
}

static mj_outs follow_two(void) {
    uint64_t l = (uint64_t)2 << 40;
    mj_replica r;
    mj_replica_init(&r);
    mj_node *f = mj_node_new(cfg(1), r);
    mj_outs o = mj_node_recv(f, 0, mj_frame_state(l, MESH_JOBS_ST_PART, 1, 0, 0, submit(1, 10)), 1, 1);
    merge_into(&o, mj_node_recv(f, 0, mj_frame_state(l, MESH_JOBS_ST_REPLACE_END, 1, 0, 1, (mj_ops){0}), 1, 1));
    mj_node_free(f);
    return o;
}

/* Review finding 4: no acknowledgement leaves without the append and sync before it. */
static void persist_first(void) {
    mj_outs all = follow_two();
    CHECK_U(acks(&all), 1);
    for (int k = 1; k <= 2; k++) {
        disk d = {.fail_append = k};
        mj_disk dk = {&d, app, syn};
        mj_outs o = follow_two();
        bool ok;
        char *err = NULL;
        mj_outs sent_ = mj_durable(&o, &dk, &ok, &err);
        CHECK(!ok && acks(&sent_) == 0);
        free(err);
        mj_outs_free(&sent_);
    }
    disk d3 = {.fail_sync = true};
    mj_disk dk3 = {&d3, app, syn};
    mj_outs o = follow_two();
    bool ok;
    char *err = NULL;
    mj_outs s3 = mj_durable(&o, &dk3, &ok, &err);
    CHECK(!ok && acks(&s3) == 0);
    free(err);
    mj_outs_free(&s3);
    disk d = {0};
    mj_disk dk = {&d, app, syn};
    mj_outs s4 = mj_durable(&all, &dk, &ok, &err);
    CHECK(ok && acks(&s4) == 1 && d.synced && d.appends == 2);
    mj_outs_free(&s4);
}

int main(void) {
    snapshot_ack();
    part_lost();
    no_snapshot_before_merge();
    answer_packs();
    persist_first();
    return mesh_check_done();
}
