/* jobs.rs's Rate and lease_time. */
#include "mesh/jobs/rate.h"

void mj_rate_count(mj_rate *r, uint64_t n, uint64_t now) {
    if (r->t0 == 0) r->t0 = now;
    r->k += n;
    uint64_t dt = now > r->t0 ? now - r->t0 : 0;
    if (dt >= 400) {
        double inst = (double)r->k * 1000.0 / (double)dt;
        r->ips = r->ips == 0.0 ? inst : 0.5 * r->ips + 0.5 * inst;
        r->t0 = now;
        r->k = 0;
    }
}

void mj_rate_busy(mj_rate *r, uint64_t now) {
    if (r->idle_at > 0 && (now > r->idle_at ? now - r->idle_at : 0) > MESH_JOBS_IDLE_MS) {
        r->t0 = now;
        r->k = 0;
    }
    r->idle_at = 0;
}

void mj_rate_lease_done(mj_rate *r, uint64_t n, uint64_t issued, uint64_t now, bool last) {
    if (r->ips == 0.0 && now > issued) r->ips = (double)n * 1000.0 / (double)(now - issued);
    /* Slow start: a lease done in under half LEASE_MS (end to end) doubles the next (if it was of the
     * full size: a lease cut short by the job's tail says nothing); one over 3 x LEASE_MS halves it. */
    uint64_t t = now > issued ? now - issued : 0;
    if (t * 2 < MESH_JOBS_LEASE_MS && n * 2 >= r->target) {
        uint64_t m = r->target > n ? r->target : n;
        r->target = m * 2 < MESH_JOBS_LEASE_MAX ? m * 2 : MESH_JOBS_LEASE_MAX;
    } else if (t > 3 * MESH_JOBS_LEASE_MS) r->target = n / 2 > 1 ? n / 2 : 1;
    if (last) r->idle_at = now > 1 ? now : 1;
}

uint64_t mj_lease_time(uint64_t n, double rate) {
    if (rate > 0.0) {
        double t = (double)n * 1000.0 / rate;
        /* Rust's `as u64` saturates (and a NaN is 0). */
        uint64_t ms = t != t ? 0 : t >= 18446744073709551615.0 ? UINT64_MAX : (uint64_t)t;
        return ms + 1;
    }
    return 10000;
}
