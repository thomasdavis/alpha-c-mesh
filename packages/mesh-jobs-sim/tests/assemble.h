/* jobs.rs's `done_jobs_assemble_until_forgotten`, shared by fleet_assemble_*.c: the invariant the live
 * fleet broke (job 2.1, rendered after 1.2 and 1.3 of the same scene, lost ~88 % of its blobs when those
 * were forgotten, while it was listed as done): a job listed as done can always be assembled to its
 * reference image, on any member, until it is forgotten -- across members killed mid-push and restarted,
 * a leader change, other jobs of the same scene (byte-identical payloads) forgotten, and a store capped
 * tiny (member 3, "the Mac": room for one job's distinct bytes). Three jobs of one scene, each through the
 * demo's chaos (a member killed at 15 %, back at 30 %, the leader killed at 45 %, back at 80 %), then
 * the older two forgotten. */
#ifndef ASSEMBLE_H
#define ASSEMBLE_H

#include "payload.h"
#include "pathtrace/digest.h"

static inline void done_jobs_assemble(uint64_t seed) {
    mj_setup setup[5];
    fpay *pays;
    size_t items;
    fs_small_scene(setup, &pays, &items);
    uint32_t sc[PT_DEFAULT_SCENE_WORDS];
    masm_job_scene(setup, 5, sc, NULL);
    uint32_t tiles = pt_tiles(sc), ts[64];
    for (uint32_t t = 0; t < tiles; t++) ts[t] = t;
    mesh_pt_tile *ref = mxalloc(tiles * sizeof *ref);
    CHECK(mesh_pt_tiles(sc, ts, tiles, 64, 0, ref));
    uint32_t *acc = mxalloc((size_t)tiles * 3 * PT_TILE_PIX * 4), *rgb = mxalloc((size_t)tiles * PT_TILE_PIX * 4);
    for (uint32_t t = 0; t < tiles; t++) {
        memcpy(acc + (size_t)t * 3 * PT_TILE_PIX, ref[t].acc, 3 * PT_TILE_PIX * 4);
        memcpy(rgb + (size_t)t * PT_TILE_PIX, ref[t].rgb, PT_TILE_PIX * 4);
        mesh_pt_tile_free(&ref[t]);
    }
    free(ref);
    uint64_t want_acc = pt_fnv_words(acc, (size_t)tiles * 3 * PT_TILE_PIX), want_rgb = pt_fnv_words(rgb, (size_t)tiles * PT_TILE_PIX);
    free(acc);
    free(rgb);
    uint64_t len = pays[0].n;
    fsim s;
    fs_init(&s, 4, 1000 + seed);
    s.payload = true;
    s.real = pays;
    s.nreal = items;
    /* Member 3's store: room for 1.5 jobs' distinct bytes (the three jobs share theirs). */
    mb_store_free(s.m[3].blobs);
    s.m[3].blobs = mb_open(NULL, items * len * 3 / 2, NULL);
    s.caps[3] = items * len * 3 / 2;
    uint32_t all[4] = {0, 1, 2, 3};
    fs_progs(&s, all, 4, 8);
    for (int k = 0; k < 4; k++) s.m[k].speed = 0.04 + 0.02 * (double)(s.rng % 3);
    fs_step(&s, 4000);
    mj_job_id ids[3];
    char line[64];
    for (uint64_t run = 0; run < 3; run++) {
        uint32_t l = (uint32_t)fs_leader(&s);
        snprintf(line, sizeof line, "submit p items %zu", items);
        mj_job_id id = fs_submit(&s, l, line);
        ids[run] = id;
        /* The member killed: the seed's pick of the others (the "Mac" every other run). */
        uint32_t victim = (seed + run) % 2 == 0 && l != 3 ? 3 : (l + 1 + (uint32_t)(seed % 3)) % 4;
        at_pct a15 = {id, 15, items}, a30 = {id, 30, items}, a45 = {id, 45, items}, a80 = {id, 80, items};
        fs_until(&s, "15 %", 60000, fs_at_pct, &a15);
        fs_crash(&s, victim);
        fs_until(&s, "30 %", 60000, fs_at_pct, &a30);
        fs_restart(&s, victim);
        fs_progs(&s, &victim, 1, 8);
        fs_until(&s, "45 %", 60000, fs_at_pct, &a45);
        l = (uint32_t)fs_leader(&s);
        fs_crash(&s, l);
        fs_until(&s, "a new leader", 10000, fs_has_leader, NULL);
        fs_until(&s, "80 %", 60000, fs_at_pct, &a80);
        fs_restart(&s, l);
        fs_progs(&s, &l, 1, 8);
        uint32_t l2 = (uint32_t)fs_leader(&s);
        mj_job *j = fs_finish(&s, l2, id, 120000);
        fs_exactly_once(j);
        mj_job_free(j);
    }
    fs_step(&s, 6000);
    /* Every job done and listed assembles to the reference on every member (the non-leaders fetch). */
    uint32_t l = (uint32_t)fs_leader(&s);
    for (int i = 0; i < 3; i++)
        for (uint32_t k = 0; k < 4; k++) {
            char *err = NULL;
            masm *a = fs_assemble(&s, k, ids[i], setup, 5, &err);
            uint64_t da = 0, dp = 0;
            if (a) masm_digests(a, &da, &dp);
            if (!CHECK(a && masm_complete(a) && da == want_acc && dp == want_rgb))
                fprintf(stderr, "seed %llu: job %d on member %u (leader %u): %s\n", (unsigned long long)seed, i, k, l, err ? err : "digests differ");
            free(err);
            masm_free(a);
        }
    CHECK(mb_bytes(s.m[3].blobs) <= s.caps[3]);
    /* The older two forgotten: their references go everywhere; the newest still assembles everywhere. */
    for (int i = 0; i < 2; i++) {
        char w[MJ_JOB_WORD_MAX];
        mj_job_word(ids[i], w);
        snprintf(line, sizeof line, "forget %s", w);
        bool ok;
        free(fs_answer(&s, fs_ask(&s, l, line), 5000, &ok));
        CHECK(ok);
    }
    fs_step(&s, 2000);
    for (uint32_t k = 0; k < 4; k++) {
        mj_job_id *js;
        size_t nj = mb_jobs(s.m[k].blobs, &js);
        for (size_t i = 0; i < nj; i++)
            if (!CHECK(js[i] != ids[0] && js[i] != ids[1])) fprintf(stderr, "seed %llu: member %u keeps a forgotten job's blobs\n", (unsigned long long)seed, k);
        free(js);
        char *err = NULL;
        masm *a = fs_assemble(&s, k, ids[2], setup, 5, &err);
        uint64_t da = 0, dp = 0;
        if (a) masm_digests(a, &da, &dp);
        if (!CHECK(a && masm_complete(a) && da == want_acc && dp == want_rgb))
            fprintf(stderr, "seed %llu: after the forgets, member %u: %s\n", (unsigned long long)seed, k, err ? err : "digests differ");
        free(err);
        masm_free(a);
    }
    /* Every holder the newest job's results name still holds its own copy. */
    const mj_job *j = mj_table_get(&s.m[l].node->rep.committed, ids[2]);
    uint64_t x;
    for (const mj_res *r = momap_first(j->results, &x); r; r = momap_next(j->results, &x, &x))
        CHECK((r->blob.holders & ~fs_held_by(&s, ids[2], &r->blob.hash)) == 0 && fs_ones(r->blob.holders) >= 2);
    fs_free(&s);
}

#endif
