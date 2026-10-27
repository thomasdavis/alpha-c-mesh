/* @ajax/mesh-rt-linalg: dense linear algebra at node scope in the exact mode (a GEMM tile and
 * a GEMV slab-staged over shared memory).
 *
 * Every output is the sequential sum over k from +0.0 of a correctly rounded
 * multiply, then a correctly rounded add (the build refuses a fused op), so a
 * tile's bits do not depend on the tile shape, the node or the GPU.
 * mesh_ref::matmul and mesh_ref::gpt2 are the CPU references. Used by
 * programs/matmul (GEMM, GEMV) and programs/gpt2x (GPT-2's projections).
 * Ported from alpha-rust-mesh crates/mesh-rt/src/linalg.rs. */
#ifndef MESH_RT_LINALG_H
#define MESH_RT_LINALG_H

#include <stdbool.h>
#include <stdint.h>

#include "mesh/rt/block.h"
#include "mesh/rt/ctx.h"

/* C is computed in TILE x TILE tiles, k in slabs of BK through shared memory. */
#define MESH_LINALG_TILE 64u
#define MESH_LINALG_BK 16u
/* The tile kernel's node: 256 threads (8 warps), each 4 x 4 outputs. */
#define MESH_LINALG_TILE_THREADS 256u
/* Shared f32s a tile needs: A's and B's slab. */
#define MESH_LINALG_TILE_SHARED (2u * MESH_LINALG_BK * MESH_LINALG_TILE)
/* Columns a GEMV node computes, and the rows of B it stages per slab. */
#define MESH_LINALG_GEMV_COLS 64u
#define MESH_LINALG_GEMV_SLAB 64u

typedef void (*mesh_linalg_store2)(uint32_t r, uint32_t c, float v, void *user);
typedef void (*mesh_linalg_store1)(uint32_t c, float v, void *user);

MESH_RT_INLINE uint32_t mesh_linalg_div_ceil(uint32_t a, uint32_t b) { return a / b + (a % b != 0); }
MESH_RT_INLINE uint32_t mesh_linalg_min(uint32_t a, uint32_t b) { return a < b ? a : b; }

/* C tile `tile` (row-major tiles) of A (m x k, row-major) times B (k x n,
 * row-major), on every thread of a 256-thread node; store(r, c, acc) for each
 * of the thread's outputs inside C (rows ty + 16 i, columns tx + 16 j). sa, sb:
 * BK x TILE f32s each. False (nothing done) for a tile outside C.
 * a and b hold m x k and k x n f32s. */
MESH_RT_INLINE bool mesh_linalg_gemm_tile(const mesh_ctx *ctx, const float *a, const float *b, uint32_t m, uint32_t k, uint32_t n,
                                          uint32_t tile, mesh_shared sa, mesh_shared sb, mesh_linalg_store2 store, void *user) {
    const uint32_t TILE = MESH_LINALG_TILE, BK = MESH_LINALG_BK, TT = MESH_LINALG_TILE_THREADS;
    uint32_t tn = mesh_linalg_div_ceil(n, TILE);
    if (tile >= mesh_linalg_div_ceil(m, TILE) * tn) return false;
    uint32_t r0 = (tile / tn) * TILE, c0 = (tile % tn) * TILE;
    uint32_t t = mesh_ctx_thread(ctx);
    uint32_t ty = t / 16, tx = t % 16;
    float acc[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) acc[i][j] = 0.0f;
    for (uint32_t k0 = 0; k0 < k; k0 += BK) {
        uint32_t kmax = mesh_linalg_min(k - k0, BK);
        for (uint32_t q = 0; q < 4; q++) {
            uint32_t e = t + TT * q;
            /* A: 64 rows x 16 k, stored k-major (As[kk][row]); B: 16 k x 64 columns. */
            uint32_t row = e / BK, kk = e % BK;
            float va = (r0 + row < m && kk < kmax) ? a[(size_t)(r0 + row) * k + k0 + kk] : 0.0f;
            mesh_shared_set_unchecked_f32(sa, kk * TILE + row, va);
            uint32_t kk2 = e / TILE, col = e % TILE;
            float vb = (kk2 < kmax && c0 + col < n) ? b[(size_t)(k0 + kk2) * n + c0 + col] : 0.0f;
            mesh_shared_set_unchecked_f32(sb, kk2 * TILE + col, vb);
        }
        mesh_ctx_block_barrier(ctx);
        for (uint32_t kk = 0; kk < kmax; kk++) {
            float av[4], bv[4];
            for (uint32_t i = 0; i < 4; i++) av[i] = mesh_shared_get_unchecked_f32(sa, kk * TILE + ty + 16 * i);
            for (uint32_t j = 0; j < 4; j++) bv[j] = mesh_shared_get_unchecked_f32(sb, kk * TILE + tx + 16 * j);
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j++) acc[i][j] = acc[i][j] + av[i] * bv[j];
        }
        mesh_ctx_block_barrier(ctx);
    }
    for (uint32_t i = 0; i < 4; i++)
        for (uint32_t j = 0; j < 4; j++) {
            uint32_t r = r0 + ty + 16 * i, c = c0 + tx + 16 * j;
            if (r < m && c < n) store(r, c, acc[i][j], user);
        }
    return true;
}

/* Thread t's PER values of slab k0 of B (element e = t + 256 q: row e / COLS,
 * column e % COLS) and x[k0 + t]. */
#define MESH_LINALG_GEMV_PER (MESH_LINALG_GEMV_SLAB * MESH_LINALG_GEMV_COLS / MESH_LINALG_TILE_THREADS)
MESH_RT_INLINE void mesh_linalg_gemv_load(const float *b, const float *x, uint32_t k, uint32_t n, uint32_t c0, uint32_t t,
                                          uint32_t k0, float v[MESH_LINALG_GEMV_PER], float *xv) {
    uint32_t rows = mesh_linalg_min(k - k0, MESH_LINALG_GEMV_SLAB);
    for (uint32_t q = 0; q < MESH_LINALG_GEMV_PER; q++) {
        uint32_t e = t + MESH_LINALG_TILE_THREADS * q;
        uint32_t r = e / MESH_LINALG_GEMV_COLS, c = e % MESH_LINALG_GEMV_COLS;
        v[q] = (r < rows && c0 + c < n) ? b[(size_t)(k0 + r) * n + c0 + c] : 0.0f;
    }
    *xv = t < rows ? x[k0 + t] : 0.0f;
}

/* Row `row` of A (m x k) times B (k x n), columns c0 .. c0 + GEMV_COLS: the
 * same sequential sum as gemm_tile's, bitwise, for the single-row case
 * (decode). The 256 threads of the node stage slabs of GEMV_SLAB rows of B
 * (and of the row of A) through registers into shared memory -- the next
 * slab's loads in flight while the first GEMV_COLS threads sum their column
 * over this one in k order. sb: GEMV_SLAB x GEMV_COLS f32s, sx: GEMV_SLAB.
 * store(c, acc). The node has TILE_THREADS threads; row < m. */
MESH_RT_INLINE void mesh_linalg_gemv_cols(const mesh_ctx *ctx, const float *a, const float *b, uint32_t k, uint32_t n, uint32_t row,
                                          uint32_t c0, mesh_shared sb, mesh_shared sx, mesh_linalg_store1 store, void *user) {
    const uint32_t COLS = MESH_LINALG_GEMV_COLS, SLAB = MESH_LINALG_GEMV_SLAB, TT = MESH_LINALG_TILE_THREADS;
    uint32_t t = mesh_ctx_thread(ctx);
    const float *x = a + (size_t)row * k;
    float acc = 0.0f;
    float regs[MESH_LINALG_GEMV_PER], rx;
    mesh_linalg_gemv_load(b, x, k, n, c0, t, 0, regs, &rx);
    for (uint32_t k0 = 0; k0 < k; k0 += SLAB) {
        uint32_t rows = mesh_linalg_min(k - k0, SLAB);
        for (uint32_t q = 0; q < MESH_LINALG_GEMV_PER; q++) mesh_shared_set_unchecked_f32(sb, t + TT * q, regs[q]);
        if (t < SLAB) mesh_shared_set_unchecked_f32(sx, t, rx);
        mesh_ctx_block_barrier(ctx);
        if (k0 + SLAB < k) mesh_linalg_gemv_load(b, x, k, n, c0, t, k0 + SLAB, regs, &rx);
        if (t < COLS) {
            /* 8 rows' operands read first, then their adds in order (the chain is the adds only). */
            uint32_t r = 0;
            while (r + 8 <= rows) {
                float xs[8], bs[8];
                for (uint32_t i = 0; i < 8; i++) xs[i] = mesh_shared_get_unchecked_f32(sx, r + i);
                for (uint32_t i = 0; i < 8; i++) bs[i] = mesh_shared_get_unchecked_f32(sb, (r + i) * COLS + t);
                for (int i = 0; i < 8; i++) acc = acc + xs[i] * bs[i];
                r += 8;
            }
            while (r < rows) {
                acc = acc + mesh_shared_get_unchecked_f32(sx, r) * mesh_shared_get_unchecked_f32(sb, r * COLS + t);
                r += 1;
            }
        }
        mesh_ctx_block_barrier(ctx);
    }
    if (t < COLS && c0 + t < n) store(c0 + t, acc, user);
}

#endif
