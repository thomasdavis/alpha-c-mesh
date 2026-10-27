/* fleet.rs's election tests over the simulated network: a leader is elected and commits (n = 3, 4,
 * 5), a minority refuses changes, a 2 + 2 split has no leader, the pair's fixed coordinator, terms and
 * votes survive restarts, and the randomised run over 300 seeds (partitions, crashes, restarts,
 * message loss, disks that fill up). */
#include "sim.h"

static uint64_t committed_version(sim_t *s, uint32_t k) { return mfc_committed(s->cores[k])->version; }

static void elected_and_commits(void) {
    for (uint32_t n = 3; n <= 5; n++) {
        sim_t s;
        sim_init(&s, n, 7 + n, 0);
        sim_step(&s, 5000);
        int l = sim_leader(&s);
        CHECK(l >= 0);
        char *err = NULL;
        uint64_t v = sim_propose(&s, (uint32_t)l, "a", &err);
        CHECK(v > 0);
        sim_step(&s, 1000);
        for (uint32_t k = 0; k < n; k++) CHECK_U(committed_version(&s, k), v);
        /* The leader dies: another leads within ~3.6 s (its link closed: sooner). */
        sim_crash(&s, (uint32_t)l);
        uint64_t t0 = s.now;
        while (sim_leader(&s) < 0 && s.now - t0 < 4000) sim_step(&s, 25);
        int l2 = sim_leader(&s);
        CHECK(l2 >= 0 && l2 != l);
        sim_step(&s, 500);
        uint64_t v2 = sim_propose(&s, (uint32_t)l2, "b", &err);
        CHECK(v2 > 0);
        sim_step(&s, 1000);
        /* It comes back: catches up, and does not disturb the new leader (pre-vote). */
        uint64_t term = mfc_term(s.cores[l2]);
        sim_restart(&s, (uint32_t)l);
        sim_step(&s, 3000);
        CHECK_I(sim_leader(&s), l2);
        CHECK_U(mfc_term(s.cores[l2]), term);
        uint64_t want = v2 > committed_version(&s, (uint32_t)l2) ? v2 : committed_version(&s, (uint32_t)l2);
        CHECK_U(committed_version(&s, (uint32_t)l), want);
        CHECK_I(s.failed, 0);
        sim_free(&s);
    }
}

static void minority_refuses(void) {
    sim_t s;
    sim_init(&s, 5, 11, 0);
    sim_step(&s, 5000);
    int l = sim_leader(&s);
    CHECK(l >= 0);
    uint32_t mate = ((uint32_t)l + 1) % 5;
    for (uint32_t k = 0; k < 5; k++) s.side[k] = (k == (uint32_t)l || k == mate) ? 1 : 0;
    sim_relink(&s);
    sim_step(&s, 4000);
    CHECK(mfc_role_of(s.cores[l]) != MFC_LEADER);
    uint64_t v;
    mfc_outs out;
    char *err = NULL;
    CHECK(!mfc_propose(s.cores[l], (mfd_doc){0}, 0, &v, &out, &err));
    CHECK(mt_contains(err, "leader"));
    free(err);
    int l2 = sim_leader(&s);
    CHECK(l2 >= 0 && l2 != l && (uint32_t)l2 != mate);
    uint64_t vm = sim_propose(&s, (uint32_t)l2, "majority", &err);
    CHECK(vm > 0);
    sim_step(&s, 500);
    for (uint32_t k = 0; k < 5; k++) s.side[k] = 0;
    sim_relink(&s);
    sim_step(&s, 4000);
    for (uint32_t k = 0; k < 5; k++) CHECK(committed_version(&s, k) >= vm);
    CHECK(sim_leader(&s) >= 0);
    CHECK_I(s.failed, 0);
    sim_free(&s);
}

static void two_two_split(void) {
    sim_t s;
    sim_init(&s, 4, 3, 0);
    sim_step(&s, 5000);
    for (uint32_t k = 0; k < 4; k++) s.side[k] = k / 2;
    sim_relink(&s);
    sim_step(&s, 6000);
    for (uint32_t k = 0; k < 4; k++) CHECK(mfc_role_of(s.cores[k]) != MFC_LEADER);
    for (uint32_t k = 0; k < 4; k++) s.side[k] = 0;
    sim_relink(&s);
    sim_step(&s, 6000);
    CHECK(sim_leader(&s) >= 0);
    sim_free(&s);
}

static void randomised(uint64_t from, uint64_t to) {
    uint64_t terms = 0, versions = 0, stops = 0, count = to - from;
    for (uint64_t seed = from; seed < to; seed++) {
        uint32_t n = 3 + (uint32_t)(seed % 3);
        sim_t s;
        sim_init(&s, n, seed * 7919, seed % 4 * 5);
        char *err = NULL;
        for (int round = 0; round < 40; round++) {
            uint64_t r = sim_rand(&s, 10);
            if (r == 0 || r == 1) {
                for (uint32_t k = 0; k < n; k++) s.side[k] = (uint32_t)sim_rand(&s, 2);
                sim_relink(&s);
            } else if (r == 2) {
                for (uint32_t k = 0; k < n; k++) s.side[k] = 0;
                sim_relink(&s);
            } else if (r == 9 && sim_rand(&s, 3) == 0) {
                /* A disk that fills up: the member stops at its next write (fail-stop). */
                uint32_t k = (uint32_t)sim_rand(&s, n);
                s.bad_disk[k] = true;
            } else if (r == 3) {
                uint32_t k = (uint32_t)sim_rand(&s, n);
                uint32_t live = 0;
                for (uint32_t j = 0; j < n; j++) live += s.cores[j] != NULL;
                if (s.cores[k] && live > 1) sim_crash(&s, k);
            } else if (r == 4) {
                uint32_t k = (uint32_t)sim_rand(&s, n);
                if (!s.cores[k]) sim_restart(&s, k);
            } else {
                int l = sim_leader(&s);
                if (l >= 0) {
                    char tag[16];
                    snprintf(tag, sizeof tag, "r%d", round);
                    if (!sim_propose(&s, (uint32_t)l, tag, &err)) free(err);
                }
            }
            uint64_t ms = 200 + sim_rand(&s, 3000);
            sim_step(&s, ms);
        }
        /* Heal everything: a leader, and every member on one committed document. */
        for (uint32_t k = 0; k < n; k++) {
            s.side[k] = 0;
            s.bad_disk[k] = false;
            if (!s.cores[k]) sim_restart(&s, k);
        }
        sim_relink(&s);
        s.loss = 0;
        sim_step(&s, 8000);
        int l = sim_leader(&s);
        if (!CHECK(l >= 0)) fprintf(stderr, "seed %llu: no leader after healing\n", (unsigned long long)seed);
        if (l >= 0) {
            uint64_t v = sim_propose(&s, (uint32_t)l, "final", &err);
            CHECK(v > 0);
            sim_step(&s, 2000);
            for (uint32_t k = 0; k < n; k++)
                if (!CHECK_U(committed_version(&s, k), v)) fprintf(stderr, "seed %llu member %u\n", (unsigned long long)seed, k);
        }
        if (!CHECK_I(s.failed, 0)) fprintf(stderr, "seed %llu broke an invariant\n", (unsigned long long)seed);
        terms += s.nleaders;
        versions += s.ncommitted;
        stops += s.fail_stops;
        sim_free(&s);
    }
    CHECK(stops > count / 10);
    CHECK(terms > 3 * count && versions > 5 * count);
    fprintf(stderr, "randomised: %llu seeds, %llu leaderships, %llu committed versions, %llu fail-stops\n", (unsigned long long)count,
            (unsigned long long)terms, (unsigned long long)versions, (unsigned long long)stops);
}

static void pair_fixed(void) {
    sim_t s;
    sim_init(&s, 2, 5, 0);
    sim_step(&s, 100);
    CHECK_I(sim_leader(&s), 0);
    char *err = NULL;
    uint64_t v = sim_propose(&s, 0, "x", &err);
    sim_step(&s, 500);
    CHECK_U(committed_version(&s, 1), v);
    /* System 1 never leads; system 0 commits alone while 1 is away, and 1 catches up on return. */
    sim_crash(&s, 1);
    uint64_t v2 = sim_propose(&s, 0, "y", &err);
    sim_step(&s, 100);
    CHECK_U(committed_version(&s, 0), v2);
    sim_restart(&s, 1);
    sim_step(&s, 2000);
    CHECK_U(committed_version(&s, 1), v2);
    sim_crash(&s, 0);
    sim_step(&s, 10000);
    CHECK_I(sim_leader(&s), -1);
    uint64_t x;
    mfc_outs out;
    CHECK(!mfc_propose(s.cores[1], (mfd_doc){0}, s.now, &x, &out, &err));
    free(err);
    sim_free(&s);
}

static void terms_and_votes_survive(void) {
    sim_t s;
    sim_init(&s, 3, 99, 0);
    sim_step(&s, 5000);
    int l = sim_leader(&s);
    CHECK(l >= 0);
    uint64_t t = mfc_term(s.cores[l]);
    /* Every member restarts from its disk: the term is never lower, and the vote of the term is kept. */
    for (uint32_t k = 0; k < 3; k++) {
        mfp_persist before = mfp_clone(&s.disk[k]);
        sim_crash(&s, k);
        sim_restart(&s, k);
        uint32_t voted = 0;
        bool hv = mfc_voted(s.cores[k], &voted);
        CHECK(mfc_term(s.cores[k]) == before.term && hv == before.has_voted && (!hv || voted == before.voted));
        CHECK(mfc_term(s.cores[k]) >= t || k != (uint32_t)l);
        mfp_free(&before);
    }
    sim_step(&s, 8000);
    CHECK(sim_leader(&s) >= 0);
    sim_free(&s);
}

int main(int argc, char **argv) {
    elected_and_commits();
    minority_refuses();
    two_two_split();
    pair_fixed();
    terms_and_votes_survive();
    /* `election 2000`: the Rust's ignored 2000-seed run. */
    randomised(1, argc > 1 ? (uint64_t)atoi(argv[1]) + 1 : 300);
    return mesh_check_done();
}
