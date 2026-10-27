/* jobs.rs's fleet tests, part 3: a new leader resumes the job (five seeds, one program each), and
 * review finding 5: a restarted leader never overwrites a committed result. */
#ifndef LEADER_H
#define LEADER_H

#include "fsim.h"

static const uint32_t ALL3[] = {0, 1, 2};

static inline void a_new_leader_resumes_the_job(uint64_t seed) {
    {
        fsim s;
        fs_init(&s, 3, seed);
        fs_progs(&s, ALL3, 3, 8);
        fs_step(&s, 4000);
        uint32_t l = (uint32_t)fs_leader(&s);
        mj_job_id id = fs_submit(&s, l, "submit p items 40000");
        fs_step(&s, 1500 + seed % 7 * 100);
        uint64_t before = mj_ranges_len(&mj_table_get(&s.m[(l + 1) % 3].node->rep.committed, id)->done);
        if (!CHECK(before > 0)) fprintf(stderr, "seed %llu: nothing committed before the crash\n", (unsigned long long)seed);
        fs_crash(&s, l);
        /* A new leader within ~4 s; it merges the survivors' tables and finishes the job. */
        fs_until(&s, "a new leader", 8000, fs_has_leader, NULL);
        uint32_t l2 = (uint32_t)fs_leader(&s);
        CHECK(l2 != l);
        mj_job *j = fs_finish(&s, l2, id, 120000);
        fs_exactly_once(j);
        if (!CHECK(j->dupes < 2000)) fprintf(stderr, "seed %llu: %llu duplicates\n", (unsigned long long)seed, (unsigned long long)j->dupes);
        mj_job_free(j);
        /* The old leader returns: it gets the table and agrees. */
        fs_restart(&s, l);
        fs_step(&s, 6000);
        CHECK_U(momap_len(mj_table_get(&s.m[l].node->rep.committed, id)->results), 40000);
        /* A second job, under the new leader, with every member. */
        fs_progs(&s, &l, 1, 8);
        mj_job_id id2 = fs_submit(&s, l2, "submit p items 3000");
        j = fs_finish(&s, l2, id2, 60000);
        fs_exactly_once(j);
        mj_job_free(j);
        fs_free(&s);
    }
}

static inline bool has_pending_result(const mj_replica *r, uint64_t item, uint32_t result) {
    for (size_t i = 0; i < r->npending; i++) {
        const mj_entry *e = &r->pending[i];
        if (e->replace) continue;
        for (size_t k = 0; k < e->ops.n; k++)
            if (e->ops.v[k].kind == MJ_OP_DONE && e->ops.v[k].item == item && e->ops.v[k].res.result == result) return true;
    }
    return false;
}

static inline bool leads_again_logged(const fsim *s, uint32_t a) {
    char want[64];
    snprintf(want, sizeof want, "system %u leads the job table:", a);
    for (size_t i = s->nlogs >= 5 ? s->nlogs - 5 : 0; i < s->nlogs; i++)
        if (strstr(s->logs[i], want)) return true;
    return false;
}

/* Review finding 5: a leader appends a result locally and crashes before sending it; another leader
 * commits that item's (other) result; the first restarts and leads again. Its uncommitted result stays
 * pending through its replay, and the merge applies the committed tables first: the committed result is
 * never overwritten (fs_record checks every member every 200 ms). */
static inline void a_restarted_leader_never_overwrites_a_committed_result(void) {
    fsim s;
    fs_init(&s, 3, 101);
    fs_progs(&s, ALL3, 3, 8);
    for (int k = 0; k < 3; k++) s.m[k].speed = 0.2;
    fs_step(&s, 4000);
    uint32_t a = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, a, "submit p items 3000");
    fs_step(&s, 500);
    /* A result of its own for an item nobody ran yet (another value than the item's), persisted locally only. */
    uint64_t x = 2999;
    CHECK(!mj_job_settled(mj_table_get(mj_node_working(s.m[a].node), id), x));
    s.m[a].has_crash_item = true;
    s.m[a].crash_item = x;
    fs_job_out(&s, a, mj_node_result(s.m[a].node, (mj_item_res){.lease = 7, .job = id, .item = x, .result = 999999}, s.now));
    uint64_t t0 = s.now;
    while (s.m[a].up) {
        fs_step(&s, 1);
        if (s.now - t0 >= 1000) abort();
    }
    mj_replica r;
    CHECK(mj_replay(s.m[a].disk.p, s.m[a].disk.n, &r, NULL, NULL, NULL));
    CHECK(has_pending_result(&r, x, 999999));                                   /* on its disk, pending */
    CHECK(momap_get1(mj_table_get(&r.committed, id)->results, x) == NULL); /* and not committed */
    mj_replica_free(&r);
    /* Another leader runs the job to its end: item x committed with its real result. */
    fs_until(&s, "a new leader", 8000, fs_has_leader, NULL);
    uint32_t b = (uint32_t)fs_leader(&s);
    for (int k = 0; k < 3; k++) s.m[k].speed = 4.0;
    mj_job *j = fs_finish(&s, b, id, 60000);
    CHECK_U(((const mj_res *)momap_get1(j->results, x))->result, (uint32_t)(x * 7 + 1));
    mj_job_free(j);
    /* The first one restarts as the second dies, and leads again: its uncommitted result meets the
     * committed one in its merge. (Back while the second still leads, its job frames lost meanwhile.) */
    fs_step(&s, 500);
    s.drop = 1000;
    fs_restart(&s, a);
    fs_progs(&s, &a, 1, 8);
    fs_step(&s, 1500);
    fs_crash(&s, b);
    s.drop = 0;
    fs_until(&s, "a new leader", 8000, fs_has_leader, NULL);
    if (!CHECK_I(fs_leader(&s), (int)a)) fprintf(stderr, "system %u leads again?\n", a);
    CHECK(leads_again_logged(&s, a));
    fs_restart(&s, b);
    fs_progs(&s, &b, 1, 8);
    fs_step(&s, 2000);
    for (int k = 0; k < 3; k++) CHECK_U(((const mj_res *)momap_get1(mj_table_get(&s.m[k].node->rep.committed, id)->results, x))->result, (uint32_t)(x * 7 + 1));
    fs_exactly_once(mj_table_get(&s.m[a].node->rep.committed, id));
    fs_free(&s);
}


#endif
