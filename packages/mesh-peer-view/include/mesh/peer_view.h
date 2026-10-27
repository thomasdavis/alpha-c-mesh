/* @ajax/mesh-peer-view: two small pieces of the fleet thread's scheduler glue (peer_sched.rs):
 * - a view the target of a migration asked its source for, arriving as F_VIEW chunks: the total must
 *   not pass the program's view (`limit`) and must not change between chunks, a chunk must not pass the
 *   total, and a duplicate chunk counts once (review finding 11);
 * - a member's load smoothed over its samples (one a second, ~5 samples' time constant), so a
 *   tenant's one-second spike does not look like a busy member to the rebalancer. */
#ifndef MESH_PEER_VIEW_H
#define MESH_PEER_VIEW_H

#include "mesh/range_set.h"
#include "mesh/sched_member.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t from;
    uint64_t deadline;
    uint8_t *buf;
    bool has_total;
    size_t total; /* the view's size as the first chunk said */
    size_t limit;
    rs_set have;  /* the byte ranges received */
} mpv_wait;

mpv_wait mpv_wait_new(uint32_t from, uint64_t deadline, size_t limit);
void mpv_wait_free(mpv_wait *w);
/* A chunk: 1 when the view is whole, 0 when not yet, -1 with *err when the chunks do not make one. */
int mpv_take(mpv_wait *w, uint32_t total, uint32_t off, const uint8_t *b, size_t n, char **err);

#define MPV_LOAD_ALPHA 0.2f

typedef struct {
    uint64_t at;
    float cpu;
    mof32 psi_cpu, psi_mem, share;
} mpv_load;

void mpv_load_add(mpv_load *l, uint64_t at, float cpu, mof32 psi_cpu, mof32 psi_mem, mof32 share);

#endif
