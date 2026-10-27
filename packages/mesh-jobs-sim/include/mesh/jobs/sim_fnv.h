/* @ajax/mesh-jobs-sim: helpers shared by the simulated-fleet tests. */
#ifndef MESH_JOBS_SIM_FNV_H
#define MESH_JOBS_SIM_FNV_H

#include <stddef.h>
#include <stdint.h>

static inline void mjs_fnv_into(uint64_t *h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        *h ^= b[i];
        *h *= 0x100000001b3u;
    }
}

static inline uint64_t mjs_fnv(const void *p, size_t n) {
    uint64_t h = 0xcbf29ce484222325u;
    mjs_fnv_into(&h, p, n);
    return h;
}

#endif
