/* @ajax/mesh-sched-weights: the fleet scheduler's score weights and rebalancing policy
 * (sched.rs Weights): the defaults, and the cluster config's `sched_*` keys over them. */
#ifndef MESH_SCHED_WEIGHTS_H
#define MESH_SCHED_WEIGHTS_H

#include "mesh/strmap.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double perf, load, mem, spread, rtt, prefer, sticky, owner;
    /* A move needs the alternative to score this much better (0.25: 25 %) ... */
    double improve;
    /* ... for this long in a row. */
    uint64_t hold_ms;
    /* A program moved stays at least this long. */
    uint64_t dwell_ms;
    /* Migrations a minute, at most. */
    uint32_t per_min;
    /* A member down this long loses its placements (rides out link blips). */
    uint64_t replace_ms;
    /* The owner policy: share under / PSI over these for owner_ms makes a member busy. */
    float owner_share, owner_psi;
    uint64_t owner_ms;
    /* The rebalancer's period. */
    uint64_t tick_ms;
    /* The CPU and memory left out of every member's reservation budget. */
    float reserve;
} msw_weights;

msw_weights msw_default(void);
/* The keys sched_improve, sched_hold_ms, sched_dwell_ms, sched_per_min, sched_replace_ms,
 * sched_owner_share, sched_owner_psi, sched_owner_ms, sched_tick_ms (at least 100) and
 * sched_reserve over the defaults. On error *err is the Rust's message. */
bool msw_from_keys(const ms_map *keys, msw_weights *out, char **err);
bool msw_eq(const msw_weights *a, const msw_weights *b);

#endif
