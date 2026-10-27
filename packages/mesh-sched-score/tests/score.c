/* sched_tests.rs: filter_keeps_the_first_reason, node_shape_filters_members (the filter part),
 * scores_follow_share_speed_load_and_prefer. */
#include "fixture.h"
#include "mesh/check.h"
#include "mesh/sched_score.h"

#include <math.h>
#include <stdlib.h>

static char *why(const msm_member *m, const msn_needs *n) {
    char exec[16], *err = NULL;
    if (msc_filter(m, n, exec, &err)) return mt_fmt("ok %s", exec);
    return err;
}

#define CHECK_WHY(m, n, want)            \
    do {                                 \
        char *w_ = why((m), (n));        \
        CHECK_S(w_, want);               \
        free(w_);                        \
    } while (0)

#define CHECK_WHY_PREFIX(m, n, prefix)   \
    do {                                 \
        char *w_ = why((m), (n));        \
        CHECK(mt_starts(w_, prefix));    \
        free(w_);                        \
    } while (0)

int main(void) {
    msm_member ms[4];
    fleet4(ms);
    /* filter_keeps_the_first_reason */
    msn_needs n = needs("metal", NULL, NULL);
    CHECK_WHY(&ms[3], &n, "ok metal");
    CHECK_WHY(&ms[0], &n, "backends metal: it runs cuda,wasm,native");
    msn_needs cw = needs("cuda", "wasm", NULL);
    CHECK_WHY(&ms[3], &cw, "ok wasm");
    ms[1].up = false;
    ms[1].down_ms = 4200;
    CHECK_WHY(&ms[1], &n, "down for 4.2 s");
    ms[0].cordoned = true;
    CHECK_WHY(&ms[0], &n, "cordoned");
    msn_needs a = needs("wasm", NULL, NULL);
    a.arch = mt_strdup("x86_64");
    CHECK(msc_eligible(&ms[2], &a));
    CHECK_WHY_PREFIX(&ms[3], &a, "arch x86_64");
    msn_needs mm = needs("wasm", NULL, NULL);
    mm.min_mem_mb = 200000;
    char *w = why(&ms[2], &mm);
    CHECK(mt_contains(w, "120000 MiB"));
    free(w);
    msn_needs v = needs("wasm", NULL, NULL);
    msn_terms_push(&v.avoid, term(MSN_HOST, "being", 0));
    CHECK_WHY(&ms[2], &v, "avoid host:being");
    CHECK(msc_eligible(&ms[3], &v) && !msc_eligible(&ms[2], &v));
    msn_free(&n), msn_free(&cw), msn_free(&a), msn_free(&mm), msn_free(&v);
    for (int i = 0; i < 4; i++) msm_member_free(&ms[i]);

    /* node_shape_filters_members */
    fleet4(ms);
    msn_needs big = needs("cuda", "metal", NULL);
    big.warps = 16;
    big.shared = 48 << 10;
    CHECK(msc_eligible(&ms[0], &big) && msc_eligible(&ms[2], &big));
    w = why(&ms[3], &big);
    CHECK(mt_starts(w, "shared_bytes 49152: ") && mt_contains(w, "allows 32768"));
    free(w);
    msn_needs fits = msn_clone(&big);
    fits.shared = 16 << 10;
    CHECK_WHY(&ms[3], &fits, "ok metal");
    msn_needs wide = msn_clone(&fits);
    wide.warps = 32;
    CHECK(msc_eligible(&ms[3], &wide));
    msm_member tiny = msm_member_clone(&ms[0]);
    tiny.max_threads = 512;
    CHECK_WHY_PREFIX(&tiny, &wide, "warps 32: 1024 threads per node");
    msm_member_free(&tiny);
    msn_needs cpu = msn_clone(&big);
    mt_strs_free(&cpu.backends);
    mt_push(&cpu.backends, "wasm");
    mt_push(&cpu.backends, "metal");
    CHECK_WHY(&ms[3], &cpu, "ok wasm");
    msm_member unknown = msm_member_clone(&ms[3]);
    unknown.max_threads = 0;
    CHECK(msc_eligible(&unknown, &big));
    msm_member_free(&unknown);
    msn_free(&big), msn_free(&fits), msn_free(&wide), msn_free(&cpu);

    /* scores_follow_share_speed_load_and_prefer */
    msw_weights wt = msw_default();
    msn_needs g = needs("cuda", "metal", NULL);
    msm_member hi = msm_member_clone(&ms[0]);
    hi.gpu_share = mof32_some(0.95f);
    CHECK(msc_score(&hi, &g, NULL, &wt) > msc_score(&ms[0], &g, NULL, &wt));
    msm_member_free(&hi);
    msm_perf fast = {.hash = 1, .exec = "cuda", .task_us = {true, 100.0f}};
    msm_perf slow = fast;
    slow.task_us = mof32_some(400.0f);
    msc_cx cf = {.w = &wt, .here = &fast, .best_task_us = mof32_some(100.0f)};
    msc_cx cs = cf;
    cs.here = &slow;
    msc_score_t sf = msc_score_terms(&ms[0], &g, &cf);
    msc_score_t ss = msc_score_terms(&ms[0], &g, &cs);
    CHECK(sf.perf > ss.perf && !sf.explore);
    CHECK(fabs(ss.perf - 0.25 * 0.5) < 1e-6);
    msc_cx none = msc_cx_simple(&wt, NULL);
    CHECK(msc_score_terms(&ms[0], &g, &none).explore);
    msn_needs c = needs("wasm", NULL, NULL);
    msn_terms_push(&c.prefer, term(MSN_HOST, "dgx-00", 0));
    msm_member hot = msm_member_clone(&ms[1]);
    hot.cpu_util = 1.0f;
    hot.psi_cpu = mof32_some(0.4f);
    msc_score_t sh = msc_score_terms(&hot, &c, &none);
    CHECK(sh.load < 1e-9 && fabs(sh.perf - 0.6) < 1e-6);
    CHECK(fabs(msc_score_terms(&ms[0], &c, &none).prefer - wt.prefer) < 1e-9);
    msm_member busy = msm_member_clone(&ms[1]);
    busy.busy = mt_strdup("x");
    CHECK(fabs(msc_score_terms(&busy, &c, &none).owner + wt.owner) < 1e-9);
    char *e = msc_explain(&sh);
    CHECK(mt_contains(e, "cpu share 0.60"));
    free(e);
    e = msc_explain(&sf);
    CHECK_S(e, "1.418 = perf 0.50 load 0.52 mem 0.20 spread 0.20 (cuda, gpu share 0.50, 100 us/task, cpu after 0.10)");
    free(e);
    msm_member_free(&hot), msm_member_free(&busy);
    msn_free(&g), msn_free(&c);
    for (int i = 0; i < 4; i++) msm_member_free(&ms[i]);
    return mesh_check_done();
}
