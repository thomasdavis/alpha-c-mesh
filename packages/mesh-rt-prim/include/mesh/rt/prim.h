/* @ajax/mesh-rt-prim: mesh-rt's device primitives for whichever target is being compiled:
 * inline PTX on nvptx64 (@ajax/mesh-rt-ptx), the CPU simulator elsewhere
 * (@ajax/mesh-rt-sim), plus the f32 helpers built on them.
 *
 * Ported from alpha-rust-mesh crates/mesh-rt/src/ptx.rs. */
#ifndef MESH_RT_PRIM_H
#define MESH_RT_PRIM_H

#include <stdbool.h>
#include <stdint.h>

#ifndef MESH_RT_INLINE
#define MESH_RT_INLINE static inline __attribute__((always_inline))
#endif

#if defined(__NVPTX__)
#include "mesh/rt/ptx.h"
#else
#include "mesh/rt/sim.h"
#endif

/* f32 <-> its bits (Rust's to_bits / from_bits). */
MESH_RT_INLINE uint32_t mesh_f32_bits(float f) {
    union { float f; uint32_t u; } x;
    x.f = f;
    return x.u;
}
MESH_RT_INLINE float mesh_f32_from_bits(uint32_t u) {
    union { uint32_t u; float f; } x;
    x.u = u;
    return x.f;
}

MESH_RT_INLINE float mesh_rt_rdf(const uint8_t *p) { return mesh_f32_from_bits(mesh_rt_rd(p)); }
MESH_RT_INLINE void mesh_rt_wrf(uint8_t *p, float v) { mesh_rt_wr(p, mesh_f32_bits(v)); }
MESH_RT_INLINE float mesh_rt_shfl_idx_f(float v, uint32_t src) { return mesh_f32_from_bits(mesh_rt_shfl_idx(mesh_f32_bits(v), src)); }

/* Butterfly sum: lane l adds lane l ^ m for m = 16, 8, 4, 2, 1. The pair sums
 * are commutative, so every lane ends with the bitwise-same value. */
MESH_RT_INLINE float mesh_rt_warp_sum(float v) {
    for (uint32_t m = 16; m > 0; m >>= 1) v += mesh_f32_from_bits(mesh_rt_shfl_bfly(mesh_f32_bits(v), m));
    return v;
}

#endif
