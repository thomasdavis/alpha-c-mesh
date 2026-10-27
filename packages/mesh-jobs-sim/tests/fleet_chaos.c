/* jobs.rs's `exactly_once_through_crashes_drops_and_clock_jumps`: exactly-once committed results
 * through leader crashes (some right after a local append, before anything is sent), member restarts,
 * dropped job frames, full disks (fail-stop) and the clock going backwards, over 40 seeds (review
 * findings 4, 5, 6, 9, 10). fs_record checks every member every 200 ms. (The Rust's 400-seed variant
 * is #[ignore]d; set MESH_CHAOS_SEEDS=400 to run as many.) */
#include "fsim.h"

int main(void) {
    uint64_t seeds = 41;
    const char *env = getenv("MESH_CHAOS_SEEDS");
    if (env) seeds = strtoull(env, NULL, 10) + 1;
    uint64_t drops = 0, stops = 0, crashes = 0;
    for (uint64_t seed = 1; seed < seeds; seed++) {
        uint32_t n = seed % 3 == 0 ? 5 : 3;
        fsim s;
        fs_init(&s, n, seed * 7919);
        uint32_t all[5] = {0, 1, 2, 3, 4};
        fs_progs(&s, all, n, 8);
        fs_step(&s, 4000);
        int l = fs_leader(&s);
        if (!CHECK(l >= 0)) {
            fprintf(stderr, "seed %llu: no leader\n", (unsigned long long)seed);
            abort();
        }
        char line[64];
        snprintf(line, sizeof line, "submit p items %llu", (unsigned long long)(3000 + seed % 5 * 1000));
        mj_job_id id = fs_submit(&s, (uint32_t)l, line);
        for (int round = 0; round < 12; round++) {
            uint32_t k = (uint32_t)fs_rand(&s, n);
            switch (fs_rand(&s, 8)) {
            case 0:
            case 1: { /* A crash (of the leader, often), keeping a majority up. */
                if (fs_rand(&s, 2) == 0) {
                    int ld = fs_leader(&s);
                    if (ld >= 0) k = (uint32_t)ld;
                }
                if (s.m[k].up && fs_up(&s) > n / 2 + 1) {
                    fs_crash(&s, k);
                    crashes++;
                }
                break;
            }
            case 2: { /* The leader appends, and crashes before anything after it is sent. */
                int ld = fs_leader(&s);
                if (ld >= 0 && fs_up(&s) > n / 2 + 1) {
                    s.m[ld].crash_after_persist = true;
                    crashes++;
                }
                break;
            }
            case 3: /* A full disk: the member stops at its next append. */
                if (s.m[k].up && fs_up(&s) > n / 2 + 1) s.m[k].bad_disk = true;
                break;
            case 4:
            case 5: /* Back, with the clock behind (an NTP step). */
                if (!s.m[k].up) {
                    s.unix_ms -= fs_rand(&s, 600000);
                    fs_restart(&s, k);
                    fs_progs(&s, &k, 1, 8);
                }
                break;
            case 6: s.drop = fs_rand(&s, 40); break;
            default: s.drop = 0;
            }
            uint64_t ms = 300 + fs_rand(&s, 1500);
            fs_step(&s, ms);
        }
        /* Heal: everyone back, nothing dropped; the job finishes with every result exactly once. */
        s.drop = 0;
        for (uint32_t k = 0; k < n; k++) {
            s.m[k].bad_disk = false;
            s.m[k].crash_after_persist = false;
            if (!s.m[k].up) {
                fs_restart(&s, k);
                fs_progs(&s, &k, 1, 8);
            }
        }
        fs_until(&s, "a leader after healing", 20000, fs_has_leader, NULL);
        l = fs_leader(&s);
        mj_job *j = fs_finish(&s, (uint32_t)l, id, 180000);
        fs_exactly_once(j);
        fs_step(&s, 3000);
        for (uint32_t k = 0; k < n; k++) {
            const mj_job *x = mj_table_get(&s.m[k].node->rep.committed, id);
            if (!CHECK(x && mj_job_eq(x, j))) fprintf(stderr, "seed %llu member %u\n", (unsigned long long)seed, k);
        }
        /* Every result any member ever showed committed is the one that stayed. */
        uint64_t key[2];
        for (const uint32_t *r = momap_first(s.seen, key); r; r = momap_next(s.seen, key, key)) {
            if (key[0] != id) continue;
            const mj_res *res = momap_get1(j->results, key[1]);
            if (!CHECK(res && res->result == *r)) fprintf(stderr, "seed %llu item %llu\n", (unsigned long long)seed, (unsigned long long)key[1]);
        }
        mj_job_free(j);
        drops += s.dropped;
        stops += s.fail_stops;
        fs_free(&s);
    }
    fprintf(stderr, "chaos: %llu crashes, %llu fail-stops, %llu job frames dropped\n", (unsigned long long)crashes, (unsigned long long)stops,
            (unsigned long long)drops);
    CHECK(crashes > 0 && stops > 0 && drops > 0);
    return mesh_check_done();
}
