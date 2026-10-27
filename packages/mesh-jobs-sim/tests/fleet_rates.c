/* jobs.rs's fleet tests, part 2: adaptive leases, a lost member and an expired lease, stealing from a
 * slow member, an endless job's state staying flat. */
#include "fsim.h"

static const uint32_t ALL3[] = {0, 1, 2};

static uint64_t biggest(const fsim *s, int k) {
    uint64_t b = 0;
    for (size_t i = 0; i < s->m[k].ngot; i++)
        if (s->m[k].got[i].n > b) b = s->m[k].got[i].n;
    return b;
}

static void adaptive_leases_follow_the_rate(void) {
    fsim s;
    fs_init(&s, 3, 5);
    fs_progs(&s, ALL3, 3, 8);
    s.m[1].speed = 8.0;
    s.m[2].speed = 0.5;
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, l, "submit p items 60000");
    mj_job *j = fs_finish(&s, l, id, 120000);
    fs_exactly_once(j);
    /* The fast member's leases grew to ~LEASE_MS at its rate (8/ms -> ~2000), the slow one's stayed small. */
    if (!CHECK(biggest(&s, 1) >= 1000)) fprintf(stderr, "fast member's largest lease %llu\n", (unsigned long long)biggest(&s, 1));
    if (!CHECK(biggest(&s, 2) <= 300)) fprintf(stderr, "slow member's largest lease %llu\n", (unsigned long long)biggest(&s, 2));
    CHECK(s.m[1].ran > 5 * s.m[2].ran);
    /* Healthy members never see a lease expire. */
    CHECK(!fs_logged(&s, "expired", NULL));
    if (!CHECK(j->dupes < 50)) fprintf(stderr, "%llu dupes\n", (unsigned long long)j->dupes);
    mj_job_free(j);
    fs_free(&s);
}

static void a_lost_member_and_an_expired_lease_are_requeued(void) {
    fsim s;
    fs_init(&s, 3, 21);
    fs_progs(&s, ALL3, 3, 8);
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, l, "submit p items 30000");
    fs_step(&s, 800);
    /* A member that is not the leader dies mid-job. */
    uint32_t victim = (l + 1) % 3;
    CHECK(s.m[victim].ran > 0);
    fs_crash(&s, victim);
    fs_step(&s, 1500);
    /* Another one hangs: alive (it beats) but runs nothing: its lease expires. */
    uint32_t hang = (l + 2) % 3;
    s.m[hang].speed = 0.0;
    mj_job *j = fs_finish(&s, l, id, 120000);
    fs_exactly_once(j);
    CHECK(momap_len(j->fails) > 0); /* the hung member's expired lease counted tries */
    CHECK_U(momap_len(j->failed), 0);
    mj_job_free(j);
    fs_free(&s);
}

static void an_idle_member_steals_from_a_slow_one(void) {
    fsim s;
    fs_init(&s, 1, 23);
    fs_free(&s);
    fs_init(&s, 3, 29);
    fs_progs(&s, ALL3, 3, 8);
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    /* Learn the rates on a first job (all equal), then one member becomes slow mid-job. */
    mj_job_id id = fs_submit(&s, l, "submit p items 6000");
    mj_job_free(fs_finish(&s, l, id, 60000));
    uint32_t slow = (l + 1) % 3;
    id = fs_submit(&s, l, "submit p items 3000");
    fs_step(&s, 300);
    s.m[slow].speed = 0.02;
    uint64_t t0 = s.now;
    mj_job *j = fs_finish(&s, l, id, 60000);
    fs_exactly_once(j);
    /* It finished long before the slow member could have run its lease: an idle member stole its tail. */
    if (!CHECK(s.now - t0 < 8000)) fprintf(stderr, "%llu ms\n", (unsigned long long)(s.now - t0));
    char from[32];
    snprintf(from, sizeof from, "from system %u", slow);
    CHECK(fs_logged(&s, "steals", from));
    mj_job_free(j);
    fs_free(&s);
}

/* Review finding 7: an endless job's runs fold into per-member counts behind the done prefix, so the
 * table, its snapshot and the (compacted) log do not grow with the items done. */
static void an_endless_job_stays_flat(void) {
    fsim s;
    fs_init(&s, 3, 61);
    fs_progs(&s, ALL3, 3, 8);
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, l, "submit p items 0- results none");
    uint64_t at[3][5];
    int i = 0;
    for (uint64_t secs[] = {14, 24, 44}, *sp = secs; sp < secs + 3; sp++, i++) {
        while (s.now < *sp * 1000) fs_step(&s, 100);
        const mj_job *j = mj_table_get(&s.m[l].node->rep.committed, id);
        mj_ops ops = {0};
        mj_table_ops(&s.m[l].node->rep.committed, &ops);
        msb snap = {0};
        mj_encode_ops(ops.v, ops.n, &snap);
        uint64_t sum = 0, m;
        for (const uint64_t *c = momap_first(j->by_member, &m); c; c = momap_next(j->by_member, &m, &m)) sum += *c;
        at[i][0] = mj_ranges_len(&j->done);
        at[i][1] = momap_len(j->runs);
        at[i][2] = mj_ranges_count(&j->done);
        at[i][3] = snap.n;
        at[i][4] = sum;
        fprintf(stderr, "t=%llus done %llu runs %llu done-ranges %llu snapshot %llu B\n", (unsigned long long)*sp, (unsigned long long)at[i][0],
                (unsigned long long)at[i][1], (unsigned long long)at[i][2], (unsigned long long)at[i][3]);
        msb_free(&snap);
        mj_ops_free(&ops);
    }
    CHECK(at[2][0] > 2 * at[0][0]);
    for (i = 0; i < 3; i++) {
        /* Runs and ranges: only the frontier; the snapshot a few hundred bytes. */
        CHECK(at[i][1] <= 12 && at[i][2] <= 12 && at[i][3] < 1000);
        /* The per-member counts still add up to every item done. */
        CHECK_U(at[i][4], at[i][0]);
    }
    /* Every replica folded the same (committed tables equal). */
    fs_step(&s, 1000);
    mj_job *j = mj_job_clone(mj_table_get(&s.m[l].node->rep.committed, id));
    for (int k = 0; k < 3; k++)
        if (s.m[k].node->rep.commit_lid == s.m[l].node->rep.commit_lid && s.m[k].node->rep.commit_n == s.m[l].node->rep.commit_n)
            CHECK(mj_job_eq(mj_table_get(&s.m[k].node->rep.committed, id), j));
    /* A snapshot rebuilds the same job (the fold travels as Op::Fold). */
    mj_table t;
    mj_table_init(&t);
    mj_ops ops = {0};
    mj_table_ops(&s.m[l].node->rep.committed, &ops);
    for (size_t q = 0; q < ops.n; q++) mj_table_apply(&t, &ops.v[q]);
    CHECK(mj_job_eq(mj_table_get(&t, id), j));
    mj_ops_free(&ops);
    mj_table_free(&t);
    mj_job_free(j);
    fs_free(&s);
}

int main(void) {
    adaptive_leases_follow_the_rate();
    a_lost_member_and_an_expired_lease_are_requeued();
    an_idle_member_steals_from_a_slow_one();
    an_endless_job_stays_flat();
    return mesh_check_done();
}
