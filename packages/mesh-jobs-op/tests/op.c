/* Ops: one of each kind encoded exactly as the Rust encodes them, decoded back, and decode's answers
 * to bad bytes; parts of at most PART_BYTES. */
#include "mesh/jobs/op.h"
#include "mesh/check.h"
#include "gold_op.h"

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

int main(void) {
    mj_setup su = {6, 0};
    mj_spec s = {.id = (uint64_t)1 << 32 | 1, .program = mstrdup("p"), .op = 254, .items = mj_ranges_of(0, 10), .attempts = 3,
                 .mode = MJ_MODE_COLLECT, .setup = mj_setup_clone(&su, 1), .nsetup = 1, .submitted_ms = 5, .members = 5};
    mj_res r7 = res(7);
    mj_tally tl[] = {{1, 4}, {2, 5}};
    mj_ops ops = {0};
    mj_ops_push(&ops, mj_op_submit(&s));
    mj_ops_push(&ops, mj_op_done(s.id, 3, &r7));
    mj_ops_push(&ops, mj_op_done_run(s.id, 4, 6, 8, 1));
    mj_ops_push(&ops, mj_op_fail(s.id, 9, 3, "boom"));
    mj_ops_push(&ops, mj_op_fails(s.id, 2, 1));
    mj_ops_push(&ops, mj_op_dupes(s.id, 4));
    mj_ops_push(&ops, mj_op_finish(s.id, 77, 1));
    mj_ops_push(&ops, mj_op_cancel(s.id, "x"));
    mj_ops_push(&ops, mj_op_tone(s.id, 2, 1));
    mj_ops_push(&ops, mj_op_fold(s.id, 9, tl, 2));
    mj_ops_push(&ops, mj_op_forget(s.id));
    msb enc = {0};
    mj_encode_ops(ops.v, ops.n, &enc);
    static uint8_t want[4096];
    size_t wn = unhexs(GOLD_OPS, want);
    CHECK_U(enc.n, wn);
    CHECK(enc.n == wn && memcmp(enc.p, want, wn) == 0);
    mj_ops back = {0};
    char *err = NULL;
    CHECK(mj_decode_ops(enc.p, enc.n, &back, &err) && mj_ops_eq(&back, &ops));
    /* Clones are deep and equal. */
    mj_ops c = mj_ops_clone(&ops);
    CHECK(mj_ops_eq(&c, &ops));
    mj_ops_free(&c);
    /* Bad bytes: the Rust's messages. */
    for (size_t i = 0; i < sizeof GOLD_BAD / sizeof *GOLD_BAD; i++) {
        uint8_t b[256];
        size_t n = unhexs(GOLD_BAD[i][1], b);
        mj_ops v = {0};
        char *e = NULL;
        if (mj_decode_ops(b, n, &v, &e)) {
            char got[32];
            snprintf(got, sizeof got, "ok %zu", v.n);
            CHECK_S(got, GOLD_BAD[i][2]);
        } else {
            CHECK_S(e, GOLD_BAD[i][2]);
            free(e);
        }
        mj_ops_free(&v);
    }
    /* Parts: under PART_BYTES each until the op that crosses it; never an op split; at least one. */
    mj_parts p = mj_parts_of(NULL, 0);
    CHECK(p.n == 1 && p.v[0].n == 0);
    mj_parts_free(&p);
    mj_ops many = {0};
    mj_res big = {.lease = 1, .out = mxalloc(1000), .out_len = 1000};
    for (int i = 0; i < 3000; i++) mj_ops_push(&many, mj_op_done(1, (uint64_t)i, &big));
    p = mj_parts_of(many.v, many.n);
    CHECK_U(p.n, 3); /* 3000 x 1038 bytes = 3.0 MiB: two full parts and the rest */
    size_t total = 0, nops = 0;
    for (size_t i = 0; i < p.n; i++) {
        mj_ops part = {0};
        CHECK(mj_decode_ops(p.v[i].p, p.v[i].n, &part, NULL));
        if (i + 1 < p.n) CHECK(p.v[i].n >= MESH_JOBS_PART_BYTES && p.v[i].n < MESH_JOBS_PART_BYTES + 1038);
        total += p.v[i].n;
        nops += part.n;
        mj_ops_free(&part);
    }
    CHECK_U(nops, 3000);
    CHECK_U(total, 3000u * 1038u);
    mj_parts_free(&p);
    mj_ops_free(&many);
    mj_res_free(&big);
    mj_ops_free(&back);
    mj_ops_free(&ops);
    mj_res_free(&r7);
    mj_spec_free(&s);
    msb_free(&enc);
    return mesh_check_done();
}
