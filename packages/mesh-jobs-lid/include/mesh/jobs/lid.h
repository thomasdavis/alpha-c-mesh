/* @ajax/mesh-jobs-lid: the leadership id of the job table (jobs.rs `lid_term`, `lid_next`,
 * `lease_base`). A lid is the fleet term in its high 24 bits and a count below, so a later
 * leadership's -- even of the same term (a restarted coordinator, a leader that stepped down and
 * leads again) -- is always larger, from neither the wall clock nor the start time (review
 * finding 10). Freestanding. */
#ifndef MESH_JOBS_LID_H
#define MESH_JOBS_LID_H

#include <stdbool.h>
#include <stdint.h>

static inline uint64_t mj_lid_term(uint64_t lid) { return lid >> 40; }

/* The lid of a leader of fleet term `term` that knows leaderships up to `seen` (persisted): above
 * every one of them. False when `seen` is of a later term (this member's view of the fleet is
 * behind; it must not lead yet) or the term doesn't fit 24 bits. */
static inline bool mj_lid_next(uint64_t term, uint64_t seen, uint64_t *out) {
    if (mj_lid_term(seen) > term || term >= (uint64_t)1 << 24) return false;
    uint64_t a = term << 40, b = seen + 1;
    *out = a > b ? a : b;
    return true;
}

/* The first lease id of leadership `lid`: its term and count above 32 bits of lease numbers, so
 * leases of two leaderships never share an id. */
static inline uint64_t mj_lease_base(uint64_t lid) { return (mj_lid_term(lid) & 0xffffffu) << 40 | (lid & 0xffu) << 32; }

#endif
