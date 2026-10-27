/* @ajax/mesh-ref-gemm: the `gemm` native program's inputs and its reference (mesh-ref lib.rs `gemm`):
 * A[i][k] and B[k][j] are small exact fractions, C = A B summed in f64. */
#ifndef MESH_REF_GEMM_H
#define MESH_REF_GEMM_H

#include <stddef.h>

float mesh_gemm_a(size_t i, size_t k);
float mesh_gemm_b(size_t k, size_t j);
/* Row-major n x n A and B into a and b (n * n each). */
void mesh_gemm_inputs(size_t n, float *a, float *b);
/* C = A B in f64 into c, and per element the sum of |a| |b| (the error scale of an f32 sum) into s. */
void mesh_gemm_reference(size_t n, double *c, double *s);
/* The largest |got - ref| / (scale + 1e-30) over C (n x n): an f32 GEMM of n terms stays well under
 * 1e-5. Allocates 2 n^2 doubles; returns a NaN if it can't. */
double mesh_gemm_max_rel_err(size_t n, const float *got);

#endif
