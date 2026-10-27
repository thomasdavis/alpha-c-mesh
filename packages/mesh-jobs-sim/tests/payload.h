/* jobs.rs's payload fleet tests (plan-pathtrace-demo 1.1), shared by fleet_payload_*.c. */
#ifndef PAYLOAD_H
#define PAYLOAD_H

#include "fsim.h"

typedef struct {
    mj_job_id id;
    uint64_t pct, items;
} at_pct;

static inline bool fs_at_pct(const fsim *s, const void *cx) {
    const at_pct *a = cx;
    return fs_done_of(s, a->id) * 100 >= a->items * a->pct;
}

/* A payload result is committed only with durable copies of its blob at the member that ran it, the
 * leader and one more (three when three are up), so a committed payload stays fetchable after a member
 * that ran items is killed and then the leader is (five members). */
static inline void payload_results_survive(uint64_t seed) {
    fsim s;
    fs_init(&s, 5, seed);
    s.payload = true;
    uint32_t all[5] = {0, 1, 2, 3, 4};
    fs_progs(&s, all, 5, 8);
    fs_step(&s, 4000);
    uint32_t l = (uint32_t)fs_leader(&s);
    mj_job_id id = fs_submit(&s, l, "submit p items 30000");
    fs_step(&s, 1200);
    /* A member that ran items (not the leader) is killed. */
    int victim = -1;
    for (uint32_t k = 0; k < 5 && victim < 0; k++)
        if (k != l && s.m[k].ran > 0) victim = (int)k;
    CHECK(victim >= 0);
    const mj_job *jl = mj_table_get(&s.m[l].node->rep.committed, id);
    size_t ntheirs = 0;
    uint64_t *items = NULL;
    mj_hash *hashes = NULL;
    uint64_t x;
    for (const mj_res *r = momap_first(jl->results, &x); r; r = momap_next(jl->results, &x, &x))
        if (r->member == (uint32_t)victim) {
            items = mxrealloc(items, (ntheirs + 1) * sizeof *items);
            hashes = mxrealloc(hashes, (ntheirs + 1) * sizeof *hashes);
            items[ntheirs] = x;
            hashes[ntheirs++] = r->blob.hash;
        }
    if (!CHECK(ntheirs > 0)) fprintf(stderr, "seed %llu: the victim has no committed results\n", (unsigned long long)seed);
    fs_crash(&s, (uint32_t)victim);
    fs_step(&s, 1500);
    /* Then the leader. */
    fs_crash(&s, l);
    fs_until(&s, "a new leader", 8000, fs_has_leader, NULL);
    uint32_t l2 = (uint32_t)fs_leader(&s);
    mj_job *j = fs_finish(&s, l2, id, 120000);
    fs_exactly_once(j);
    /* Every committed payload is held by a member that is up (the victim's among them). */
    uint32_t up = 0;
    for (uint32_t k = 0; k < 5; k++)
        if (s.m[k].up) up |= (uint32_t)1 << k;
    for (const mj_res *r = momap_first(j->results, &x); r; r = momap_next(j->results, &x, &x)) {
        size_t n;
        uint8_t *want = fs_payload_of(&s, id, x, &n);
        mj_hash wh = fs_sha(want, n);
        free(want);
        if (!CHECK((fs_held_by(&s, id, &r->blob.hash) & up) != 0 && mj_hash_eq(&r->blob.hash, &wh) &&
                   ((r->blob.holders >> l2 & 1) != 0 || fs_ones(r->blob.holders) >= 3)))
            fprintf(stderr, "seed %llu: item %llu holders %#x\n", (unsigned long long)seed, (unsigned long long)x, r->blob.holders);
    }
    for (size_t i = 0; i < ntheirs; i++) CHECK((fs_held_by(&s, id, &hashes[i]) & up) != 0);
    if (!CHECK(s.min_copies >= 2)) fprintf(stderr, "seed %llu: a committed payload had %u copies\n", (unsigned long long)seed, s.min_copies);
    mj_job_free(j);
    free(items);
    free(hashes);
    /* Both return; they agree, and keep their copies. */
    fs_restart(&s, (uint32_t)victim);
    fs_restart(&s, l);
    fs_step(&s, 6000);
    CHECK_U(momap_len(mj_table_get(&s.m[victim].node->rep.committed, id)->results), 30000);
    CHECK_U(momap_len(mj_table_get(&s.m[l].node->rep.committed, id)->results), 30000);
    fs_free(&s);
}

/* Eviction pressure: distinct payloads per job and member 3's store capped at about one job's share.
 * It never evicts a listed job's copies (it refuses, and the copies go to other members), so every
 * finished job stays assemblable; once a job is forgotten its references go and member 3 takes copies
 * again. */
static inline void a_full_store_refuses(uint64_t seed) {
    fsim s;
    fs_init(&s, 4, 2000 + seed);
    s.payload = true;
    s.salt = true;
    uint64_t items = 3000, cap = items * 64;
    mb_store_free(s.m[3].blobs);
    s.m[3].blobs = mb_open(NULL, cap, NULL);
    s.caps[3] = cap;
    uint32_t all[4] = {0, 1, 2, 3};
    fs_progs(&s, all, 4, 8);
    fs_step(&s, 4000);
    /* Member 3 is not the leader (a full leader commits nothing: it holds every blob). */
    if (fs_leader(&s) == 3) {
        fs_crash(&s, 3);
        fs_until(&s, "a new leader", 10000, fs_has_leader, NULL);
        fs_restart(&s, 3);
        fs_progs(&s, (uint32_t[]){3}, 1, 8);
        fs_step(&s, 2000);
    }
    uint32_t l = (uint32_t)fs_leader(&s);
    CHECK(l != 3);
    mj_job_id ids[3];
    char line[64];
    snprintf(line, sizeof line, "submit p items %llu", (unsigned long long)items);
    for (int run = 0; run < 3; run++) {
        mj_job_id id = fs_submit(&s, l, line);
        ids[run] = id;
        if (run == 1) {
            /* A member killed and restarted mid-job. */
            at_pct a = {id, 40, items};
            fs_until(&s, "40 %", 60000, fs_at_pct, &a);
            fs_crash(&s, 1);
            fs_step(&s, 1000);
            fs_restart(&s, 1);
            fs_progs(&s, (uint32_t[]){1}, 1, 8);
        }
        mj_job *j = fs_finish(&s, l, id, 120000);
        fs_exactly_once(j);
        mj_job_free(j);
    }
    fs_step(&s, 3000);
    if (!CHECK(s.m[3].refused > 0)) fprintf(stderr, "seed %llu: member 3's store never filled\n", (unsigned long long)seed);
    CHECK(mb_bytes(s.m[3].blobs) <= cap);
    /* Every finished job's every result: its holders hold their own copies, at least two of them. */
    for (int i = 0; i < 3; i++) {
        const mj_job *j = mj_table_get(&s.m[l].node->rep.committed, ids[i]);
        uint64_t x;
        for (const mj_res *r = momap_first(j->results, &x); r; r = momap_next(j->results, &x, &x))
            CHECK((r->blob.holders & ~fs_held_by(&s, ids[i], &r->blob.hash)) == 0 && fs_ones(r->blob.holders) >= 2);
    }
    /* The first job forgotten: room on member 3, which takes copies of the next job again. */
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(ids[0], w);
    snprintf(line, sizeof line, "forget %s", w);
    bool ok;
    free(fs_answer(&s, fs_ask(&s, l, line), 5000, &ok));
    CHECK(ok);
    fs_step(&s, 1000);
    snprintf(line, sizeof line, "submit p items %llu", (unsigned long long)items);
    mj_job_id id = fs_submit(&s, l, line);
    mj_job *j = fs_finish(&s, l, id, 120000);
    fs_exactly_once(j);
    mj_job_free(j);
    mj_job_id *js;
    size_t nj = mb_jobs(s.m[3].blobs, &js);
    bool has_new = false, has_old = false;
    for (size_t i = 0; i < nj; i++) {
        has_new |= js[i] == id;
        has_old |= js[i] == ids[0];
    }
    free(js);
    if (!CHECK(has_new)) fprintf(stderr, "seed %llu: member 3 took no copy after the forget\n", (unsigned long long)seed);
    CHECK(!has_old);
    fs_free(&s);
}

#endif
