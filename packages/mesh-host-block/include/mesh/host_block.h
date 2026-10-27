/* @ajax/mesh-host-block: the host status block (status.rs HostStatus):
 * SNAP_HOST bytes, little-endian, written by meshd's threads under one lock,
 * read whole by the snapshot with the uptime and the live network counters
 * filled in at that moment (the counters' rates are timed by that uptime). */
#ifndef MESH_HOST_BLOCK_H
#define MESH_HOST_BLOCK_H

#include "mesh/abi/snapshot.h"
#include "mesh/net_counters.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    pthread_mutex_t mu;
    uint8_t block[MESH_SNAP_HOST];
    int64_t started_us;
    mesh_net_counters net; /* the live counters (links, control port, HTTP relay) */
} mesh_host_block;

/* One field of a consistent set. */
typedef struct {
    size_t off;
    const void *bytes;
    size_t len;
} mesh_host_field;

void mesh_host_block_init(mesh_host_block *h);
void mesh_host_block_set_u32(mesh_host_block *h, size_t off, uint32_t v);
void mesh_host_block_set_f32(mesh_host_block *h, size_t off, float v);
void mesh_host_block_set_u64(mesh_host_block *h, size_t off, uint64_t v);
/* Several fields under one lock (a consistent set for readers). */
void mesh_host_block_set_many(mesh_host_block *h, const mesh_host_field *f, size_t n);
/* A u32 counter += n (wrapping). */
void mesh_host_block_add_u32(mesh_host_block *h, size_t off, uint32_t n);
/* A NUL-padded string field of len bytes (truncated to len - 1 so it stays terminated). */
void mesh_host_block_set_str(mesh_host_block *h, size_t off, size_t len, const char *s);
uint64_t mesh_host_block_uptime_ms(const mesh_host_block *h);
uint32_t mesh_host_block_get_u32(mesh_host_block *h, size_t off);
float mesh_host_block_get_f32(mesh_host_block *h, size_t off);
/* The block with S_UPTIME_MS and the live network counters filled in. */
void mesh_host_block_bytes(mesh_host_block *h, uint8_t out[MESH_SNAP_HOST]);

#endif
