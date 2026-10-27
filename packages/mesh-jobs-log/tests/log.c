/* The job log: random record sequences replayed here and in the Rust to the same replica; the Rust
 * test's log (what is committed and what is not, a torn tail); review finding 6's snapshot; and
 * replay's answers to corrupt records. */
#include "mesh/jobs/log.h"
#include "mesh/check.h"
#include "gen.h"
#include "gold_log.h"

static uint64_t fnv(const void *p, size_t n) {
    const uint8_t *b = p;
    uint64_t h = 0xcbf29ce484222325u;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3u;
    }
    return h;
}

static size_t unhexs(const char *h, uint8_t *out) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    return n;
}

static void encode_logs(const mj_logs *v, msb *out) {
    for (size_t i = 0; i < v->n; i++) mj_log_encode(&v->v[i], out);
}

static void random_sequences(void) {
    size_t g = 0, ng = sizeof GOLD_SEQ / sizeof *GOLD_SEQ;
    char line[256];
    for (uint64_t seeds[] = {3, 5, 11, 99}, *sd = seeds; sd < seeds + 4; sd++) {
        rs = *sd * 0x9e3779b97f4a7c15u | 1;
        mj_replica rep;
        mj_replica_init(&rep);
        msb bytes = {0};
        for (int i = 1; i <= 400; i++) {
            uint64_t hi = 1 + rnd(3);
            uint64_t lid = hi << 40 | rnd(3);
            mj_log l;
            switch (rnd(8)) {
            case 0: l = mj_log_lid(lid); break;
            case 1: {
                uint64_t n = rnd(6), k = rnd(4);
                mj_ops ops = {0};
                for (uint64_t j = 0; j < k; j++) mj_ops_push(&ops, gen());
                l = mj_log_snapshot(lid, n, ops);
                break;
            }
            case 2:
            case 3:
            case 4: {
                uint64_t n = rnd(8);
                bool replace = rnd(4) == 0;
                uint64_t k = rnd(4);
                mj_ops ops = {0};
                for (uint64_t j = 0; j < k; j++) mj_ops_push(&ops, gen());
                l = mj_log_entry(lid, n, replace, ops);
                break;
            }
            default: l = mj_log_commit(lid, rnd(8));
            }
            mj_log_encode(&l, &bytes);
            mj_replica_apply(&rep, &l);
            mj_log_free(&l);
            if (i % 40 == 0) {
                mj_logs lg = mj_replica_log(&rep);
                msb c = {0};
                encode_logs(&lg, &c);
                snprintf(line, sizeof line, "%llu %d %016llx %zu lid %llx commit %llx,%llu pending %zu last %llu", (unsigned long long)*sd, i,
                         (unsigned long long)fnv(c.p, c.n), c.n, (unsigned long long)rep.lid, (unsigned long long)rep.commit_lid,
                         (unsigned long long)rep.commit_n, rep.npending, (unsigned long long)mj_replica_last(&rep, (uint64_t)1 << 40));
                if (CHECK(g < ng)) CHECK_S(line, GOLD_SEQ[g]);
                g++;
                /* The compacted log replays to the same replica. */
                mj_replica again;
                CHECK(mj_replay(c.p, c.n, &again, NULL, NULL, NULL) && mj_replica_eq(&again, &rep));
                mj_replica_free(&again);
                mj_logs_free(&lg);
                msb_free(&c);
            }
        }
        mj_replica rr;
        size_t n = 0, at = 0;
        CHECK(mj_replay(bytes.p, bytes.n, &rr, &n, &at, NULL) && mj_replica_eq(&rr, &rep));
        snprintf(line, sizeof line, "%llu replay %zu %zu %016llx", (unsigned long long)*sd, n, at, (unsigned long long)fnv(bytes.p, bytes.n));
        if (CHECK(g < ng)) CHECK_S(line, GOLD_SEQ[g]);
        g++;
        mj_replica_free(&rr);
        mj_replica_free(&rep);
        msb_free(&bytes);
    }
    CHECK_U(g, ng);
}

static mj_res res(uint64_t lease) {
    mj_res r = {.lease = lease, .member = 2, .result = 9, .out = mxalloc(3), .out_len = 3};
    memcpy(r.out, (uint8_t[]){1, 2, 3}, 3);
    return r;
}

static mj_ops one(mj_op o) {
    mj_ops v = {0};
    mj_ops_push(&v, o);
    return v;
}

/* The log section of the Rust's ops_frames_and_the_merge. */
static void the_rust_log(void) {
    mj_spec s = gspec(0);
    uint64_t l = (uint64_t)1 << 40;
    mj_res r1 = res(1), r3 = res(3);
    mj_table x;
    mj_table_init(&x);
    mj_op sub = mj_op_submit(&s), d1 = mj_op_done(s.id, 1, &r1);
    mj_table_apply(&x, &sub);
    mj_table_apply(&x, &d1);
    msb log = {0};
    mj_log a = mj_log_lid(l);
    mj_log_encode(&a, &log);
    mj_ops xo = {0};
    mj_table_ops(&x, &xo);
    mj_log snap = mj_log_snapshot(l, 3, xo);
    mj_log_encode(&snap, &log);
    mj_log e4 = mj_log_entry(l, 4, false, one(mj_op_done(s.id, 7, &r3)));
    mj_log e5 = mj_log_entry(l, 5, false, one(mj_op_done(s.id, 6, &r3)));
    mj_log c4 = mj_log_commit(l, 4);
    mj_log_encode(&e4, &log);
    mj_log_encode(&e5, &log);
    mj_log_encode(&c4, &log);
    static uint8_t want[8192];
    size_t wn = unhexs(GOLD_TESTLOG, want);
    CHECK(log.n == wn && memcmp(log.p, want, wn) == 0);
    size_t whole = log.n;
    mj_log e6 = mj_log_entry(l, 6, false, one(mj_op_done(s.id, 8, &r3)));
    msb e6b = {0};
    mj_log_encode(&e6, &e6b);
    msb_put(&log, e6b.p, 20);
    mj_replica r;
    size_t n, at;
    CHECK(mj_replay(log.p, log.n, &r, &n, &at, NULL));
    CHECK_U(n, 5);
    CHECK_U(at, whole);
    CHECK_U(r.lid, l);
    CHECK(r.commit_lid == l && r.commit_n == 4);
    char *w = mj_ranges_word_new(&mj_table_get(&r.committed, s.id)->done);
    CHECK_S(w, "1-2,7-8");
    free(w);
    CHECK_U(r.npending, 1); /* item 6's entry is not committed: pending, not in the table */
    /* The replica's own log replays to it (a compaction). */
    mj_logs lg = mj_replica_log(&r);
    msb c = {0};
    encode_logs(&lg, &c);
    mj_replica again;
    CHECK(mj_replay(c.p, c.n, &again, NULL, NULL, NULL) && mj_replica_eq(&again, &r));
    mj_replica_free(&again);
    mj_logs_free(&lg);
    msb_free(&c);
    mj_replica_free(&r);
    for (mj_log *p[] = {&a, &snap, &e4, &e5, &c4, &e6}, **q = p; q < p + 6; q++) mj_log_free(*q);
    msb_free(&e6b);
    msb_free(&log);
    mj_op_free(&sub);
    mj_op_free(&d1);
    mj_table_free(&x);
    mj_res_free(&r1);
    mj_res_free(&r3);
    mj_spec_free(&s);
    /* Corrupt records: the Rust's answers. */
    for (size_t i = 0; i < sizeof GOLD_BAD / sizeof *GOLD_BAD; i++) {
        uint8_t b[256];
        size_t bn = unhexs(GOLD_BAD[i][0], b);
        mj_replica q;
        char *err = NULL;
        if (mj_replay(b, bn, &q, &n, &at, &err)) {
            char got[64];
            snprintf(got, sizeof got, "ok %zu %zu", n, at);
            CHECK_S(got, GOLD_BAD[i][1]);
            mj_replica_free(&q);
        } else {
            CHECK_S(err, GOLD_BAD[i][1]);
            free(err);
        }
    }
}

/* Review finding 6 (the Rust's a_snapshot_keeps_the_entries_acknowledged_past_it, replica half). */
static void snapshot_keeps_entries(void) {
    uint64_t l = (uint64_t)3 << 40;
    mj_replica r;
    mj_replica_init(&r);
    mj_log seq[4] = {mj_log_lid(l), mj_log_snapshot(l, 8, (mj_ops){0}), mj_log_entry(l, 9, false, one(mj_op_fails(1, 9, 1))),
                     mj_log_entry(l, 10, false, one(mj_op_fails(1, 10, 1)))};
    for (int i = 0; i < 4; i++) {
        mj_replica_apply(&r, &seq[i]);
        mj_log_free(&seq[i]);
    }
    mj_spec s = gspec(0);
    mj_log s8 = mj_log_snapshot(l, 8, one(mj_op_submit(&s)));
    mj_replica_apply(&r, &s8);
    CHECK(mj_replica_last(&r, l) == 10 && r.npending == 2); /* entries 9 and 10 stay */
    mj_log s9 = mj_log_snapshot(l, 9, one(mj_op_submit(&s)));
    mj_replica_apply(&r, &s9);
    CHECK(mj_replica_last(&r, l) == 10 && r.npending == 1);
    mj_log_free(&s8);
    mj_log_free(&s9);
    mj_spec_free(&s);
    mj_replica_free(&r);
}

int main(void) {
    random_sequences();
    the_rust_log();
    snapshot_keeps_entries();
    return mesh_check_done();
}
