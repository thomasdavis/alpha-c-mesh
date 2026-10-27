/* Rate windows, slow start and lease times, step by step as jobs.rs computes them. */
#include "mesh/jobs/rate.h"
#include "mesh/check.h"

int main(void) {
    mj_rate r = {0};
    mj_rate_count(&r, 10, 100);   /* the window opens at 100 */
    CHECK(r.ips == 0.0 && r.k == 10 && r.t0 == 100);
    mj_rate_count(&r, 30, 500);   /* 40 items in 400 ms: 100/s */
    CHECK(r.ips == 100.0 && r.k == 0 && r.t0 == 500);
    mj_rate_count(&r, 120, 900);  /* 300/s: the EWMA gives 200 */
    CHECK(r.ips == 200.0);
    /* Slow start: a full-size lease done fast doubles the target; a slow one halves it. */
    mj_rate s = {.target = 8};
    mj_rate_lease_done(&s, 8, 1000, 1100, false);
    CHECK(s.ips == 80.0 && s.target == 16);
    mj_rate_lease_done(&s, 4, 2000, 2050, false); /* short of half the target: no change */
    CHECK_U(s.target, 16);
    mj_rate_lease_done(&s, 30, 3000, 3800, true);
    CHECK(s.target == 15 && s.idle_at == 3800);
    mj_rate_busy(&s, 3840);       /* within IDLE_MS: the window stays */
    CHECK(s.idle_at == 0);
    s.idle_at = 4000;
    s.k = 7;
    mj_rate_busy(&s, 4100);
    CHECK(s.t0 == 4100 && s.k == 0);
    mj_rate big = {.target = MESH_JOBS_LEASE_MAX};
    mj_rate_lease_done(&big, MESH_JOBS_LEASE_MAX, 0, 10, false);
    CHECK_U(big.target, MESH_JOBS_LEASE_MAX);
    CHECK_U(mj_lease_time(100, 0.0), 10000);
    CHECK_U(mj_lease_time(100, 400.0), 251);
    CHECK_U(mj_lease_time(3, 7.0), 429);
    return mesh_check_done();
}
