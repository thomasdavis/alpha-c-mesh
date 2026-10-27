/* @ajax/pathtrace-rng: the random numbers of programs/pathtrace, a counter-based hash (PCG) of
 * (seed, pixel, sample, dimension), so any sample is computable anywhere (pathtrace-core lib.rs).
 * Freestanding, static inline. */
#ifndef PATHTRACE_RNG_H
#define PATHTRACE_RNG_H

#include <stdint.h>

/* PCG's output hash of one step (Jarzynski & Olano 2020, "pcg"). */
static inline uint32_t pt_pcg(uint32_t v) {
    uint32_t s = v * 747796405u + 2891336453u;
    uint32_t w = ((s >> ((s >> 28) + 4)) ^ s) * 277803737u;
    return (w >> 22) ^ w;
}

/* One sample's key: (seed, pixel = y W + x, sample index). */
static inline uint32_t pt_sample_key(uint32_t seed, uint32_t pixel, uint32_t sample) {
    return pt_pcg(pt_pcg(seed + pt_pcg(pixel)) + sample);
}

/* The sample's dimension d: 0, 1 the pixel jitter; bounce b uses 2 + 8 b .. 2 + 8 b + 7. */
static inline uint32_t pt_rnd(uint32_t key, uint32_t d) { return pt_pcg(key + d * 0x9e3779b9u); }

#endif
