/* @ajax/mesh-ref-cubic: programs/train's and programs/infer's CPU reference (mesh-ref lib.rs), written
 * operation for operation as the programs compute, in the cluster float mode: a cubic
 * p0 + p1 x + p2 x^2 + p3 x^3 fitted by SGD to sin(pi x) on a grid, each step's loss and gradient sums
 * by the warp butterfly. Freestanding. */
#ifndef MESH_REF_CUBIC_H
#define MESH_REF_CUBIC_H

#include <stddef.h>
#include <stdint.h>

/* p0 + p1 x + p2 x2 + p3 x3, summed left to right (x2 = x x, x3 = x2 x). */
float mesh_cubic(const float p[4], float x);
/* The warp's butterfly sum: v[l] + v[l ^ m] for m = 16 .. 1; lane 0's result. */
float mesh_cubic_warp_sum(const float v[32]);
/* One step's 5 sums (loss, g0..g3) for sample offset off: lane l uses sample (8 l + off) % 256. */
void mesh_cubic_step_sums(const float p[4], const float *xs, const float *ys, uint32_t off, float out[5]);
/* The SGD update p[k] -= lr s[k + 1] inv, and the loss written for a step, s[0] inv. */
void mesh_cubic_apply(float p[4], const float s[5], float lr, float inv);
float mesh_cubic_loss(const float s[5], float inv);
/* programs/train's sin(pi x): f64 Horner of the Taylor series after folding into [-1/2, 1/2]; the
 * f32 -> f64 and f64 -> f32 converts flush (cvt.ftz). */
float mesh_cubic_sin_pi(float x);
/* out[i] = -1 + 2 i / (n - 1) for i < n (train's samples, infer's points). n >= 2. */
void mesh_cubic_grid(uint32_t n, float *out);
/* train's samples: xs = grid(SAMPLES), ys = sin_pi(xs); both MESH_TRAIN_SAMPLES long. */
void mesh_cubic_samples(float *xs, float *ys);

#endif
