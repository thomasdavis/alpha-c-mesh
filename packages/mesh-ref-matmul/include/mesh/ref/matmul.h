/* @ajax/mesh-ref-matmul: programs/matmul's exact mode on the CPU (mesh-ref matmul.rs), the seed of GPU
 * inference. Row-major: A is M x K, B is K x N, C is M x N.
 *
 * - C = A B: every element a sequential sum over k from +0.0 (`acc = acc + a * b`, a correctly
 *   rounded multiply then a correctly rounded add, both FTZ, never fused).
 * - y = A x: per row, lane l sums k = l, l + 32, .. sequentially from +0.0, then the warp butterfly. */
#ifndef MESH_REF_MATMUL_H
#define MESH_REF_MATMUL_H

#include <stddef.h>
#include <stdint.h>

/* Element i of the matrix with seed s: (h >> 8) 2^-23 - 1, in [-1, 1) (exact on every backend). */
float mesh_matmul_value(uint32_t seed, uint32_t i);
/* out[0..n) = value(seed, 0..n). */
void mesh_matmul_matrix(uint32_t seed, size_t n, float *out);
/* c (m x n) = a (m x k) b (k x n) in the exact order, rows split over `threads` threads (the result
 * is the same for any count). */
void mesh_matmul(const float *a, const float *b, size_t m, size_t k, size_t n, size_t threads, float *c);
/* y (m) = a (m x k) x (k) in matvec's order. */
void mesh_matvec(const float *a, const float *x, size_t m, size_t k, float *y);
/* FNV-1a 64 over the words of a matrix: matmul's digest. */
uint64_t mesh_matmul_digest(const float *c, size_t n);

#endif
