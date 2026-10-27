/* @ajax/mesh-load-share: the GPU share a program's nodes obtained (load.rs Nl,
 * Share, share, quantile): from the node load records' counters (mesh-abi NL_*:
 * time observed, time lost to someone else, gaps, queue delay), summed over a
 * program's nodes and over a window of samples. */
#ifndef MESH_LOAD_SHARE_H
#define MESH_LOAD_SHARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One node load record's counters (NL_*). */
typedef struct {
    uint64_t obs, lost;
    uint32_t gaps, queue_us;
} mesh_nl;

/* The counters' advance since prev; a counter that went down was reset (a reload zeroes them): no delta. */
mesh_nl mesh_nl_delta(const mesh_nl *now, const mesh_nl *prev);
/* Adds d (its lost time at most its observed time). */
void mesh_nl_add(mesh_nl *sum, const mesh_nl *d);

/* The figures derived from summed node load deltas. */
typedef struct {
    bool has_share; /* false: nothing observed */
    float share;
    float slice_us, gap_us; /* mean run between gaps / mean gap (0 when no gap) */
    float queue_us;         /* Metal: mean commit -> GPU start per command buffer */
} mesh_share;

mesh_share mesh_share_of(const mesh_nl *d);

/* The q-quantile (0..=1, nearest rank) of a sorted array; 0 when empty. */
uint32_t mesh_quantile(const uint32_t *sorted, size_t n, double q);

#endif
