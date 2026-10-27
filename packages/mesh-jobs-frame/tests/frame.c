/* The job frames: the eleven frames of jobs.rs's `ops_frames_and_the_merge`, encoded byte for byte
 * as the Rust encodes them and decoded back; decode's answers to bad bodies. */
#include "mesh/jobs/frame.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/peer_sizes.h"
#include "mesh/check.h"
#include "gold_frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t unhexs(const char *h, uint8_t *out) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    return n;
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

static mj_ops the_ops(const mj_spec *s) {
    mj_res r7 = res(7);
    mj_ops ops = {0};
    mj_ops_push(&ops, mj_op_submit(s));
    mj_ops_push(&ops, mj_op_done(s->id, 3, &r7));
    mj_ops_push(&ops, mj_op_done_run(s->id, 4, 6, 8, 1));
    mj_ops_push(&ops, mj_op_fail(s->id, 9, 3, "boom"));
    mj_ops_push(&ops, mj_op_fails(s->id, 2, 1));
    mj_ops_push(&ops, mj_op_dupes(s->id, 4));
    mj_ops_push(&ops, mj_op_finish(s->id, 77, 1));
    mj_ops_push(&ops, mj_op_cancel(s->id, "x"));
    mj_ops_push(&ops, mj_op_tone(s->id, 2, 1));
    mj_res_free(&r7);
    return ops;
}

static mj_held *held1(mj_job_id job) {
    mj_held *h = mxalloc(sizeof *h);
    *h = (mj_held){4, job, mstrdup("p"), mj_ranges_of(1, 5)};
    return h;
}

int main(void) {
    mj_setup su = {6, 0};
    mj_spec s = {.id = (uint64_t)1 << 32 | 1, .program = mstrdup("p"), .op = 254, .items = mj_ranges_of(0, 10), .attempts = 3,
                 .mode = MJ_MODE_COLLECT, .setup = mj_setup_clone(&su, 1), .nsetup = 1, .submitted_ms = 5, .members = 5};
    mj_frame fr[11];
    mj_prog *pg = mxalloc(sizeof *pg);
    *pg = (mj_prog){mstrdup("p"), 2, 8};
    fr[0] = mj_frame_avail(3, pg, 1, held1(s.id), 1);
    fr[1] = mj_frame_state(3, MESH_JOBS_ST_DELTA, 4, 2, 0, the_ops(&s));
    fr[2] = mj_frame_state(3, MESH_JOBS_ST_PART, 4, 2, 7, the_ops(&s));
    fr[3] = mj_frame_ack(3, 4, true);
    fr[4] = mj_frame_pull(3);
    fr[5] = mj_frame_have(3, MESH_JOBS_HAVE_DELTA, 2, 9, 4, the_ops(&s), NULL, 0);
    fr[6] = mj_frame_have(3, MESH_JOBS_HAVE_END, 3, 0, 0, (mj_ops){0}, held1(s.id), 1);
    mj_tally tl[] = {{1, 4}, {2, 5}};
    mj_ops fold = {0};
    mj_ops_push(&fold, mj_op_fold(s.id, 9, tl, 2));
    fr[7] = mj_frame_state(3, MESH_JOBS_ST_PART, 1, 0, 0, fold);
    mj_lease_msg l = {.lease = 5, .job = s.id, .program = mstrdup("p"), .op = 254};
    mj_setup ls[] = {{6, 0}, {3, 32}};
    l.setup = mj_setup_clone(ls, 2);
    l.nsetup = 2;
    CHECK(mj_ranges_parse("1-4,9", &l.items, NULL));
    fr[8] = mj_frame_lease(3, l);
    fr[9] = mj_frame_cut(3, 5, 2);
    mj_item_res *it = mxalloc(2 * sizeof *it);
    it[0] = (mj_item_res){.lease = 5, .job = s.id, .item = 1, .status = MESH_JOBS_RS_FAIL, .out = (uint8_t *)mstrdup("why"), .out_len = 3};
    it[1] = (mj_item_res){.lease = 5, .job = s.id, .item = 2, .status = MESH_JOBS_RS_OK, .result = 3, .has_blob = true};
    memset(it[1].blob.hash.b, 7, 32);
    it[1].blob.len = 9;
    it[1].blob.holders = 5;
    fr[10] = mj_frame_result(3, it, 2);
    CHECK_U(sizeof GOLD_FRAMES / sizeof *GOLD_FRAMES, 11);
    for (int i = 0; i < 11; i++) {
        msb b = {0};
        mj_frame_encode(&fr[i], &b);
        static uint8_t want[8192];
        size_t wn = unhexs(GOLD_FRAMES[i], want);
        if (!CHECK(b.n == wn && memcmp(b.p, want, wn) == 0)) fprintf(stderr, "frame %d differs\n", i);
        uint32_t len;
        memcpy(&len, b.p + 1, 4);
        CHECK_U(len, b.n - 1 - MESH_JOB_HEAD);
        mj_frame d;
        char *err = NULL;
        CHECK(mj_frame_decode((uint8_t)b.p[0], b.p + 1 + MESH_JOB_HEAD, b.n - 1 - MESH_JOB_HEAD, &d, &err) && mj_frame_eq(&d, &fr[i]));
        mj_frame c = mj_frame_clone(&d);
        CHECK(mj_frame_eq(&c, &fr[i]));
        mj_frame_free(&c);
        mj_frame_free(&d);
        msb_free(&b);
    }
    for (size_t i = 0; i < sizeof GOLD_BAD / sizeof *GOLD_BAD; i++) {
        uint8_t b[128];
        size_t n = unhexs(GOLD_BAD[i].hex, b);
        mj_frame d;
        char *err = NULL;
        if (mj_frame_decode((uint8_t)GOLD_BAD[i].ty, b, n, &d, &err)) {
            CHECK_S("ok", GOLD_BAD[i].answer);
            mj_frame_free(&d);
        } else {
            CHECK_S(err, GOLD_BAD[i].answer);
            free(err);
        }
    }
    for (int i = 0; i < 11; i++) mj_frame_free(&fr[i]);
    mj_spec_free(&s);
    return mesh_check_done();
}
