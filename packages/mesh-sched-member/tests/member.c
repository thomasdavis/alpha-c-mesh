/* executors, exec_for, rtt_to, the performance map's order and ranges. */
#include "mesh/check.h"
#include "mesh/sched_member.h"
#include "mesh/text.h"

int main(void) {
    msm_member m = msm_member_default();
    m.id = 1;
    MSM_SET(m.gpu, "metal");
    const char *ex[3];
    CHECK_U(msm_executors(&m, ex), 3);
    CHECK_S(ex[0], "metal");
    CHECK_S(ex[2], "native");
    MSM_SET(m.gpu, "wasm");
    CHECK_U(msm_executors(&m, ex), 2);
    char *b[] = {"cuda", "native", "wasm"};
    CHECK_S(msm_exec_for(&m, b, 3), "native");
    CHECK(msm_exec_for(&m, b, 1) == NULL);
    m.nrtt = 3;
    m.rtt_us[2] = mof32_some(250.0f);
    CHECK(msm_rtt_to(&m, 1).some && msm_rtt_to(&m, 1).v == 0.0f);
    CHECK(msm_rtt_to(&m, 2).v == 250.0f && !msm_rtt_to(&m, 0).some && !msm_rtt_to(&m, 5).some);
    m.busy = mt_strdup("x");
    ms_set(&m.fails, "p", "why", true);
    msm_member c = msm_member_clone(&m);
    CHECK_S(c.busy, "x");
    CHECK_S(ms_get(&c.fails, "p"), "why");
    msm_member_free(&c);
    msm_member_free(&m);
    msm_perfs p = {0};
    msm_perf a = {.hash = 1, .exec = "cuda", .task_us = {true, 100}};
    msm_perfs_set(&p, "b", 2, &a);
    msm_perfs_set(&p, "a", 3, &a);
    msm_perfs_set(&p, "b", 0, &a);
    msm_perfs_set(&p, "ab", 1, &a);
    a.hash = 9;
    msm_perfs_set(&p, "b", 2, &a);
    CHECK_U(p.n, 4);
    CHECK_S(p.name[0], "a");
    CHECK_S(p.name[1], "ab");
    CHECK(p.member[2] == 0 && p.member[3] == 2 && p.perf[3].hash == 9);
    size_t lo, hi;
    msm_perfs_range(&p, "b", &lo, &hi);
    CHECK(lo == 2 && hi == 4);
    msm_perfs_range(&p, "c", &lo, &hi);
    CHECK(lo == hi);
    CHECK(msm_perfs_get(&p, "ab", 1) != NULL && msm_perfs_get(&p, "ab", 2) == NULL);
    msm_perfs c2 = msm_perfs_clone(&p);
    msm_perfs_remove_at(&c2, 0);
    CHECK(c2.n == 3 && msm_perf_eq(&c2.perf[2], &p.perf[3]));
    msm_perfs_free(&c2);
    msm_perfs_free(&p);
    return mesh_check_done();
}
