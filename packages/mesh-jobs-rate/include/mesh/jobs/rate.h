/* @ajax/mesh-jobs-rate: a member's measured rate for a program, as the leader keeps it (jobs.rs
 * `Rate`, `lease_time` and the lease-sizing constants): items/s as an EWMA over windows of at least
 * 400 ms, and a lease target that grows by slow start. */
#ifndef MESH_JOBS_RATE_H
#define MESH_JOBS_RATE_H

#include <stdbool.h>
#include <stdint.h>

/* A member without work longer than this was idle (its rate window restarts). */
#define MESH_JOBS_IDLE_MS 50u
/* A lease's target duration at the member's measured rate. */
#define MESH_JOBS_LEASE_MS 250u
/* At most this many items in one lease. */
#define MESH_JOBS_LEASE_MAX ((uint64_t)1 << 20)

typedef struct {
    double ips; /* items/s */
    uint64_t t0, k, idle_at;
    uint64_t target; /* slow start's lease size */
} mj_rate;

/* n items done at now (ms). */
void mj_rate_count(mj_rate *r, uint64_t n, uint64_t now);
/* A lease for a member with none outstanding: if it sat idle (more than a link's round trip), the
 * next window starts now. */
void mj_rate_busy(mj_rate *r, uint64_t now);
/* A lease of n items issued at `issued` is done at now; `last`: the member has no other lease of
 * the program. The first rate is its turnaround; slow start doubles or halves the target. */
void mj_rate_lease_done(mj_rate *r, uint64_t n, uint64_t issued, uint64_t now, bool last);
/* A lease of n items at `rate` items/s takes this long (ms; an unknown rate: 10 s). */
uint64_t mj_lease_time(uint64_t n, double rate);

#endif
