/* @ajax/mesh-ref-gpt2-scalar: GPT-2's scalar functions built from + - * / and integer bit operations,
 * every float op in the cluster float mode Z (mesh-ref gpt2.rs), so every executor computes the same
 * bits. The constants as f32 bits (the Metal port writes the same words). Freestanding. */
#ifndef MESH_REF_GPT2_SCALAR_H
#define MESH_REF_GPT2_SCALAR_H

#include <stdint.h>

#define MESH_GPT2_ATTN_SCALE_BITS 0x3e000000u /* 0.125 = 1 / sqrt(head_dim) */
#define MESH_GPT2_LN_EPS_BITS 0x3727c5acu     /* 1e-5 */
#define MESH_GPT2_GELU_C_BITS 0x3f4c422au     /* sqrt(2 / pi) = 0.7978846 */
#define MESH_GPT2_GELU_A_BITS 0x3d372713u     /* 0.044715 */
#define MESH_GPT2_LOG2E_BITS 0x3fb8aa3bu      /* 1.442695 */
#define MESH_GPT2_LN2_HI_BITS 0x3f317200u     /* 0.69314575 */
#define MESH_GPT2_LN2_LO_BITS 0x35bfbe8eu     /* 1.4286068e-6 */
/* exp's Horner coefficients 1/10!, 1/9!, .., 1/2!, 1/1!, 1/0!. */
extern const uint32_t mesh_gpt2_exp_poly_coeffs[11];

/* exp: k = trunc(x log2e +- 0.5), r = (x - k ln2_hi) - k ln2_lo, a degree-10 Horner polynomial, scaled
 * by 2^k in two exact steps. 0 below -87, inf above 88. */
float mesh_gpt2_exp_poly(float x);
/* tanh = 1 - 2 / (exp(2x) + 1); +-1 beyond |x| > 10. */
float mesh_gpt2_tanh_poly(float x);
/* The tanh-form GELU ("gelu_new"): (0.5 x) (1 + tanh(c (x + ((a x) x) x))). */
float mesh_gpt2_gelu_tanh(float x);
/* sqrt of a > 0: a bit-shift first guess, then 4 Newton steps y = 0.5 (y + a / y). */
float mesh_gpt2_sqrt_newton(float a);

#endif
