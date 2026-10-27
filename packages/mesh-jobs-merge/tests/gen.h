/* The random op generator of the Rust golden tests (tests/common/mod.rs there), identical. */
#include "mesh/jobs/op.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

