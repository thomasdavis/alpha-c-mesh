/* @ajax/mesh-occupancy: co-residency of resident kernels (CUDA; mesh-host
 * backend.rs co_resident, room).  Every program's nodes must be on the GPU at
 * once, forever, so a load is refused unless all of them fit.  Three rules:
 * - each kernel: nodes <= per_sm x SMs; all: blocks <= blocks_per_sm x SMs;
 * - the occupancy shares sum(nodes / (per_sm x SMs)) <= 1;
 * - fragmentation: a node that needs a whole SM (per_sm == 1) fits only on an
 *   SM no other block touches, so the whole-SM nodes plus min(SMs, the other
 *   blocks) must not exceed the SMs. */
#ifndef MESH_OCCUPANCY_H
#define MESH_OCCUPANCY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One kernel: its nodes and the resident blocks of its shape per SM. */
typedef struct {
    uint32_t nodes;
    uint32_t per_sm;
} mesh_occ_kernel;

/* False with *err (the Rust's message) when the kernels don't fit together. */
bool mesh_co_resident(const mesh_occ_kernel *k, size_t n, uint32_t sms, uint32_t blocks_per_sm, char **err);
/* The most nodes (at most max) of a kernel of per_sm that fit beside others; 0: none. */
uint32_t mesh_occ_room(const mesh_occ_kernel *others, size_t n, uint32_t per_sm, uint32_t max, uint32_t sms, uint32_t blocks_per_sm);

#endif
