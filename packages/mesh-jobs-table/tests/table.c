/* The job table: jobs.rs's `ops_frames_and_the_merge` (its table half), and random op sequences
 * applied identically here and in the Rust, compared by the hash of the encoded table. */
#include "mesh/jobs/table.h"
#include "mesh/check.h"
#include "gold_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv(const void *p, size_t n) {
    const uint8_t *b = p;
    uint64_t h = 0xcbf29ce484222325u;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3u;
    }
    return h;
}

static mj_spec mkspec(mj_job_id id, const char *items, int32_t prio, uint32_t members, bool setup) {
    mj_spec s = {.id = id, .program = mstrdup("p"), .op = 254, .priority = prio, .attempts = 3, .mode = MJ_MODE_COLLECT, .submitted_ms = 5, .members = members};
    if (!mj_ranges_parse(items, &s.items, NULL)) abort();
    if (setup) {
        mj_setup su = {6, 0};
        s.setup = mj_setup_clone(&su, 1);
        s.nsetup = 1;
    }
    return s;
}

static mj_res res(uint64_t lease) {
    mj_res r = {.lease = lease, .member = 2, .result = 9, .out = mxalloc(3), .out_len = 3};
    memcpy(r.out, (uint8_t[]){1, 2, 3}, 3);
    if (lease % 2 == 1) {
        r.has_blob = true;
        memset(r.blob.hash.b, (int)lease, 32);
        r.blob.len = 9216;
        r.blob.holders = 0xb;
    }
    return r;
}

static void the_rust_test(void) {
    mj_spec s = mkspec((uint64_t)1 << 32 | 1, "0-10", 0, 5, true);
    mj_res r7 = res(7), r99 = res(99), r1 = res(1), r2 = res(2);
    mj_ops ops = {0};
    mj_ops_push(&ops, mj_op_submit(&s));
    mj_ops_push(&ops, mj_op_done(s.id, 3, &r7));
    mj_ops_push(&ops, mj_op_done_run(s.id, 4, 6, 8, 1));
    mj_ops_push(&ops, mj_op_fail(s.id, 9, 3, "boom"));
    mj_ops_push(&ops, mj_op_fails(s.id, 2, 1));
    mj_ops_push(&ops, mj_op_dupes(s.id, 4));
    mj_ops_push(&ops, mj_op_finish(s.id, 77, MESH_JOBS_FIN_DONE));
    mj_ops_push(&ops, mj_op_cancel(s.id, "x"));
    mj_ops_push(&ops, mj_op_tone(s.id, 2, 1));
    msb enc = {0};
    mj_encode_ops(ops.v, ops.n, &enc);
    mj_ops back = {0};
    CHECK(mj_decode_ops(enc.p, enc.n, &back, NULL) && mj_ops_eq(&back, &ops));
    mj_ops_free(&back);
    mj_table a;
    mj_table_init(&a);
    for (size_t i = 0; i < ops.n; i++) mj_table_apply(&a, &ops.v[i]);
    /* Idempotent; a second result for item 3 (another lease) does not replace the first. */
    mj_table b = mj_table_clone(&a);
    for (size_t i = 0; i < ops.n; i++) mj_table_apply(&b, &ops.v[i]);
    mj_op late = mj_op_done(s.id, 3, &r99);
    mj_table_apply(&b, &late);
    CHECK(mj_table_eq(&a, &b));
    /* The union: two tables with different items merge to both, in either order. */
    mj_table x, y;
    mj_table_init(&x);
    mj_table_init(&y);
    mj_table_apply(&x, &ops.v[0]);
    mj_table_apply(&y, &ops.v[0]);
    mj_op d1 = mj_op_done(s.id, 1, &r1), d2 = mj_op_done(s.id, 2, &r2);
    mj_table_apply(&x, &d1);
    mj_table_apply(&y, &d2);
    mj_table xy = mj_table_clone(&x), yx = mj_table_clone(&y);
    mj_ops xo = {0}, yo = {0};
    mj_table_ops(&y, &yo);
    mj_table_ops(&x, &xo);
    for (size_t i = 0; i < yo.n; i++) mj_table_apply(&xy, &yo.v[i]);
    for (size_t i = 0; i < xo.n; i++) mj_table_apply(&yx, &xo.v[i]);
    CHECK(mj_table_eq(&xy, &yx));
    char *w = mj_ranges_word_new(&mj_table_get(&xy, s.id)->done);
    CHECK_S(w, "1-3");
    free(w);
    /* The table's own ops rebuild it. */
    mj_ops ao = {0};
    mj_table_ops(&a, &ao);
    mj_table c;
    mj_table_init(&c);
    for (size_t i = 0; i < ao.n; i++) mj_table_apply(&c, &ao.v[i]);
    CHECK(mj_table_eq(&c, &a));
    uint64_t lease;
    CHECK(mj_job_lease_of(mj_table_get(&a, s.id), 5, &lease) && lease == 8);
    CHECK(mj_job_lease_of(mj_table_get(&a, s.id), 3, &lease) && lease == 7);
    CHECK(!mj_job_lease_of(mj_table_get(&a, s.id), 0, &lease));
    CHECK_S(mj_job_state(mj_table_get(&a, s.id)), "done-with-failures");
    for (mj_table *t[] = {&a, &b, &x, &y, &xy, &yx, &c}, **p = t; p < t + 7; p++) mj_table_free(*p);
    mj_ops_free(&xo);
    mj_ops_free(&yo);
    mj_ops_free(&ao);
    mj_ops_free(&ops);
    mj_op_free(&late);
    mj_op_free(&d1);
    mj_op_free(&d2);
    mj_res_free(&r7);
    mj_res_free(&r99);
    mj_res_free(&r1);
    mj_res_free(&r2);
    mj_spec_free(&s);
    msb_free(&enc);
}

/* ---- the generator the Rust golden test runs (tests/gold_table.rs) ------------------------- */

static uint64_t rs;
static uint64_t rnd(uint64_t n) {
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs % (n ? n : 1);
}

static mj_spec gspec(uint64_t k) {
    if (k == 0) return mkspec((uint64_t)1 << 32 | 1, "0-200", 0, 0, false);
    if (k == 1) return mkspec((uint64_t)1 << 32 | 2, "0-", 1, 0, false);
    return mkspec((uint64_t)2 << 32 | 1, "5-50,100-", 2, 0, false);
}

static mj_op gen(void) {
    uint64_t k = rnd(3);
    mj_spec sp = gspec(k);
    mj_job_id job = sp.id;
    mj_op o;
    switch (rnd(10)) {
    case 0: o = mj_op_submit(&sp); break;
    case 1:
    case 2:
    case 3: {
        uint64_t item = rnd(220);
        mj_res r = {0};
        r.lease = rnd(5);
        r.member = (uint32_t)rnd(4);
        r.result = (uint32_t)rnd(1000);
        r.out_len = (size_t)rnd(3);
        r.out = r.out_len ? mxalloc(r.out_len) : NULL;
        for (size_t i = 0; i < r.out_len; i++) r.out[i] = (uint8_t)rnd(256);
        if (rnd(2) == 1) {
            uint8_t f = (uint8_t)rnd(256);
            r.has_blob = true;
            memset(r.blob.hash.b, f, 32);
            r.blob.len = (uint32_t)rnd(10000);
            r.blob.holders = (uint32_t)rnd(16);
        }
        o = mj_op_done(job, item, &r);
        mj_res_free(&r);
        break;
    }
    case 4:
    case 5: {
        uint64_t s = rnd(220), e = s + rnd(30), lease = rnd(5);
        uint32_t member = (uint32_t)rnd(4);
        o = mj_op_done_run(job, s, e, lease, member);
        break;
    }
    case 6: {
        uint64_t item = rnd(220);
        uint32_t tries = (uint32_t)rnd(4);
        char why[8];
        snprintf(why, sizeof why, "w%llu", (unsigned long long)rnd(9));
        o = mj_op_fail(job, item, tries, why);
        break;
    }
    case 7: {
        uint64_t item = rnd(220);
        o = mj_op_fails(job, item, (uint32_t)rnd(5));
        break;
    }
    case 8:
        switch (rnd(4)) {
        case 0: o = mj_op_dupes(job, rnd(10)); break;
        case 1: {
            uint64_t at = rnd(100);
            o = mj_op_finish(job, at, 1 + (uint32_t)rnd(3));
            break;
        }
        case 2: {
            char why[8];
            snprintf(why, sizeof why, "c%llu", (unsigned long long)rnd(9));
            o = mj_op_cancel(job, why);
            break;
        }
        default: {
            uint32_t seq = (uint32_t)rnd(4);
            o = mj_op_tone(job, seq, (uint32_t)rnd(2));
        }
        }
        break;
    default: {
        if (rnd(20) == 0) {
            o = mj_op_forget(job);
            break;
        }
        uint64_t upto = rnd(220), n = rnd(3);
        mj_tally tl[3];
        for (uint64_t i = 0; i < n; i++) {
            tl[i].member = (uint32_t)rnd(4);
            tl[i].n = rnd(50);
        }
        o = mj_op_fold(job, upto, tl, (size_t)n);
    }
    }
    mj_spec_free(&sp);
    return o;
}

static void gold(void) {
    size_t g = 0, ng = sizeof GOLD / sizeof *GOLD;
    char line[256];
    for (uint64_t seeds[] = {1, 7, 42, 1234}, *sd = seeds; sd < seeds + 4; sd++) {
        rs = *sd * 0x9e3779b97f4a7c15u | 1;
        mj_table t;
        mj_table_init(&t);
        mj_ops all = {0};
        for (int i = 1; i <= 600; i++) {
            mj_op o = gen();
            mj_table_apply(&t, &o);
            mj_ops_push(&all, o);
            if (i % 50 == 0) {
                mj_ops to = {0};
                mj_table_ops(&t, &to);
                msb enc = {0};
                mj_encode_ops(to.v, to.n, &enc);
                mj_table c;
                mj_table_init(&c);
                for (size_t k = 0; k < to.n; k++) mj_table_apply(&c, &to.v[k]);
                mj_ops co = {0};
                mj_table_ops(&c, &co);
                msb enc2 = {0};
                mj_encode_ops(co.v, co.n, &enc2);
                snprintf(line, sizeof line, "%llu %d %016llx %zu %016llx %d", (unsigned long long)*sd, i, (unsigned long long)fnv(enc.p, enc.n), enc.n,
                         (unsigned long long)fnv(enc2.p, enc2.n), (int)mj_table_eq(&c, &t));
                if (CHECK(g < ng)) CHECK_S(line, GOLD[g]);
                g++;
                mj_table_free(&c);
                mj_ops_free(&to);
                mj_ops_free(&co);
                msb_free(&enc);
                msb_free(&enc2);
            }
        }
        msb enc = {0};
        mj_encode_ops(all.v, all.n, &enc);
        snprintf(line, sizeof line, "%llu ops %016llx %zu", (unsigned long long)*sd, (unsigned long long)fnv(enc.p, enc.n), enc.n);
        if (CHECK(g < ng)) CHECK_S(line, GOLD[g]);
        g++;
        /* Decoding the whole sequence gives it back. */
        mj_ops back = {0};
        CHECK(mj_decode_ops(enc.p, enc.n, &back, NULL) && mj_ops_eq(&back, &all));
        mj_ops_free(&back);
        snprintf(line, sizeof line, "%llu next %llx %llx", (unsigned long long)*sd, (unsigned long long)mj_table_next_id(&t, 1),
                 (unsigned long long)mj_table_next_id(&t, 2));
        if (CHECK(g < ng)) CHECK_S(line, GOLD[g]);
        g++;
        msb_free(&enc);
        mj_ops_free(&all);
        mj_table_free(&t);
    }
    CHECK_U(g, ng);
}

int main(void) {
    the_rust_test();
    gold();
    return mesh_check_done();
}
