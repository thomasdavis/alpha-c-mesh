/* @ajax/mesh-jobs-copies: who keeps a payload's durable copies before its result is sent (jobs.rs
 * `copies_for`, `COPIES`, and the resend times of pushes and fetches). Freestanding. */
#ifndef MESH_JOBS_COPIES_H
#define MESH_JOBS_COPIES_H

#include <stdbool.h>
#include <stdint.h>

/* Durable copies of a payload before its result is sent (the member that ran it and the leader
 * among them, when that many members are up). */
#define MESH_JOBS_COPIES 3u
/* A push not acknowledged is sent again after this long (ms). */
#define MESH_JOBS_PUSH_RESEND_MS 2000u
/* The leader asks the holders again for a blob it still lacks after this long (ms). */
#define MESH_JOBS_FETCH_RESEND_MS 3000u
/* Results the leader parks waiting for their blobs, at most (more are dropped: resent later). */
#define MESH_JOBS_PARKED_MAX 100000u

static inline unsigned mj_popcount(uint32_t x) {
    unsigned n = 0;
    for (; x; x &= x - 1) n++;
    return n;
}

/* Who keeps the copies of a payload that member `me` ran: `me`, the leader, then the members after
 * `me` (in id order, wrapping) that are `alive`, up to COPIES distinct members, none of `refused` (a
 * full store) but the leader. False without a leader. */
static inline bool mj_copies_for(uint32_t me, uint32_t n, bool has_leader, uint32_t leader, uint32_t alive, uint32_t refused,
                                 uint32_t *out) {
    if (!has_leader) return false;
    uint32_t want = (uint32_t)1 << leader;
    if ((refused >> me & 1) == 0) want |= (uint32_t)1 << me;
    for (uint32_t i = 1; i < n; i++) {
        uint32_t k = (me + i) % n;
        if (mj_popcount(want) >= MESH_JOBS_COPIES) break;
        if ((alive >> k & 1) != 0 && (refused >> k & 1) == 0) want |= (uint32_t)1 << k;
    }
    *out = want;
    return true;
}

#endif
