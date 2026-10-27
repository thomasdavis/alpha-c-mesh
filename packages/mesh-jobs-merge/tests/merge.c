/* The merge: random answers (committed tables at random commit points, entries of random
 * leaderships) merged here and in the Rust to the same table; an answer built from a replica. */
#include "mesh/jobs/merge.h"
#include "mesh/check.h"
#include "gen.h"
#include "gold_merge.h"

static uint64_t fnv(const void *p, size_t n) {
    const uint8_t *b = p;
    uint64_t h = 0xcbf29ce484222325u;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x100000001b3u;
    }
    return h;
}

int main(void) {
    size_t g = 0, ng = sizeof GOLD / sizeof *GOLD;
    char line[128];
    for (uint64_t seeds[] = {2, 4, 8, 16, 32}, *sd = seeds; sd < seeds + 5; sd++) {
        rs = *sd * 0x9e3779b97f4a7c15u | 1;
        for (int round = 0; round < 20; round++) {
            mj_answer slot[6];
            bool have[6] = {0};
            uint64_t k = 1 + rnd(4);
            for (uint64_t i = 0; i < k; i++) {
                uint32_t m = (uint32_t)rnd(6);
                mj_answer a;
                mj_answer_init(&a);
                uint64_t cl = rnd(3) << 40;
                a.commit_lid = cl;
                a.commit_n = rnd(4);
                uint64_t nops = rnd(30);
                for (uint64_t j = 0; j < nops; j++) {
                    mj_op o = gen();
                    mj_table_apply(&a.committed, &o);
                    mj_op_free(&o);
                }
                uint64_t ne = rnd(4);
                for (uint64_t j = 0; j < ne; j++) {
                    uint64_t lid = (1 + rnd(3)) << 40;
                    uint64_t n = rnd(5), c = rnd(6);
                    mj_ops ops = {0};
                    for (uint64_t q = 0; q < c; q++) mj_ops_push(&ops, gen());
                    mj_ops *e = mj_answer_entry(&a, lid, n);
                    mj_ops_free(e);
                    *e = ops;
                }
                if (have[m]) mj_answer_free(&slot[m]);
                slot[m] = a;
                have[m] = true;
            }
            mj_answer *in[6];
            size_t n = 0;
            for (int m = 0; m < 6; m++)
                if (have[m]) in[n++] = &slot[m];
            mj_table t = mj_merge(in, n);
            mj_ops to = {0};
            mj_table_ops(&t, &to);
            msb enc = {0};
            mj_encode_ops(to.v, to.n, &enc);
            snprintf(line, sizeof line, "%llu %d %zu %016llx %zu", (unsigned long long)*sd, round, n, (unsigned long long)fnv(enc.p, enc.n), enc.n);
            if (CHECK(g < ng)) CHECK_S(line, GOLD[g]);
            g++;
            msb_free(&enc);
            mj_ops_free(&to);
            mj_table_free(&t);
            for (int m = 0; m < 6; m++)
                if (have[m]) mj_answer_free(&slot[m]);
        }
    }
    CHECK_U(g, ng);
    /* A replica's own answer: its committed table, and each pending entry under (lid, n). */
    mj_replica r;
    mj_replica_init(&r);
    mj_spec s = gspec(0);
    mj_ops one = {0};
    mj_ops_push(&one, mj_op_submit(&s));
    mj_log e1 = mj_log_entry((uint64_t)2 << 40, 1, true, one);
    mj_replica_apply(&r, &e1);
    mj_answer a = mj_answer_of(&r);
    uint64_t key[2] = {(uint64_t)2 << 40, 1};
    mj_ops *got = momap_get(a.entries, key);
    CHECK(got && got->n == 1 && got->v[0].kind == MJ_OP_SUBMIT);
    mj_answer *in[1] = {&a};
    mj_table t = mj_merge(in, 1);
    CHECK(mj_table_get(&t, s.id) != NULL);
    mj_table_free(&t);
    mj_answer_free(&a);
    mj_log_free(&e1);
    mj_spec_free(&s);
    mj_replica_free(&r);
    return mesh_check_done();
}
