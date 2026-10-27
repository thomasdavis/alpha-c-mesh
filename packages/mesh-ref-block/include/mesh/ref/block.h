/* @ajax/mesh-ref-block: the block collectives of a multi-warp node (crates/mesh-rt/src/block.rs,
 * mesh_rt.h) on the CPU, step for step: the same trees, the same operand order, every f32 op in Z.
 * A GPU's result for thread values `vals` (thread t's value at index t, a multiple of 32 of them,
 * at most 1024) is bitwise the one these return.
 *
 * Each collective comes for f32, u32 and u64 values with the operation as a function pointer.
 * Freestanding (no libc). */
#ifndef MESH_REF_BLOCK_H
#define MESH_REF_BLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* f32 sum's identity, -0.0, and max's, all bits set (the least value of the total order), as bits. */
#define MESH_BLOCK_SUM_F32_ID_BITS 0x80000000u
#define MESH_BLOCK_MAX_F32_ID_BITS 0xffffffffu
float mesh_block_sum_f32_id(void);
float mesh_block_max_f32_id(void);

/* The larger in the IEEE total order of the bits (one of them, unchanged): mesh-rt `max_f32`. */
float mesh_block_max_f32(float a, float b);
/* add.rn.ftz.f32. */
float mesh_block_add_f32(float a, float b);
uint32_t mesh_block_add_u32(uint32_t a, uint32_t b); /* wrapping */
uint64_t mesh_block_add_u64(uint64_t a, uint64_t b); /* wrapping */

/* A 32-bit integer hash (lowbias32): the same on every backend. */
uint32_t mesh_block_mix(uint32_t x);

/* The test values programs/blocks gives thread t for seed s: an f32 of mixed signs and magnitudes
 * (a few zeros, subnormals and large values), a u32 and a u64. */
void mesh_block_test_values(uint32_t seed, uint32_t t, float *f, uint32_t *u, uint64_t *w);

#define MESH_BLOCK_DECLARE(T, S)                                                                   \
    typedef T (*mesh_block_op_##S)(T, T);                                                          \
    /* The warp butterfly, in place: lane l := op(own, lane l ^ m) for m = 16, 8, 4, 2, 1. */       \
    void mesh_block_warp_reduce_##S(T v[32], mesh_block_op_##S op);                                \
    /* The warp's inclusive Hillis-Steele scan, in place: lane l >= d := op(lane l - d, own). */    \
    void mesh_block_warp_scan_##S(T v[32], mesh_block_op_##S op);                                  \
    /* A block reduction: each warp's butterfly, then the butterfly over the 32 warp results      \
     * (`id` for the warps the node lacks). n: a multiple of 32, 32..1024 (else `id`). */          \
    T mesh_block_reduce_##S(const T *vals, size_t n, T id, mesh_block_op_##S op);                  \
    /* A block scan into out[0..n): inclusive, or exclusive (a warp's first thread gets `id` in    \
     * warp 0, else the combined prefix). False (out untouched) unless n is valid as above. */     \
    bool mesh_block_scan_##S(const T *vals, size_t n, T id, mesh_block_op_##S op, bool inclusive,  \
                             T *out);

MESH_BLOCK_DECLARE(float, f32)
MESH_BLOCK_DECLARE(uint32_t, u32)
MESH_BLOCK_DECLARE(uint64_t, u64)

#endif
