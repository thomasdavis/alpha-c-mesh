/* @ajax/pathtrace-math: the float contract of programs/pathtrace's renderer. pathtrace-core in Rust is
 * written once over `R: Real` and instantiated twice: f32 on the GPU (every op .rn.ftz by the build)
 * and mesh-ref's Z on CPUs (every op flushed). Here the same code is written once in template headers
 * (pathtrace/t_*.h) over these macros, and a translation unit instantiates it by defining them and
 * including pathtrace/instantiate.h (@ajax/pathtrace-tile). Values are always `float`; only the
 * operations differ.
 *
 * An instantiation defines:
 *   PT_SUFFIX            the name suffix of every function, e.g. _f32 (pt_sample_f32)
 *   PT_ADD(a, b) PT_SUB(a, b) PT_MUL(a, b) PT_DIV(a, b) PT_NEG(a)     each correctly rounded, unfused
 *   PT_LT(a, b) PT_GT(a, b) PT_LE(a, b) PT_GE(a, b)                   false when a NaN is involved
 * and may define PT_API (the linkage of the public functions; default static inline).
 *
 * Determinism rules (README "Cluster float mode: FTZ" of the Rust repo): only + - * / and compares on
 * floats; integer -> float converts of values < 2^24; sqrt is a Newton iteration, cis a polynomial;
 * the random numbers are a counter-based hash; accumulation orders are fixed. Compile with
 * -ffp-contract=off. */
#ifndef PATHTRACE_REAL_H
#define PATHTRACE_REAL_H

#include "pathtrace/consts.h"

#include <stddef.h>

#define PT_CAT_(a, b) a##b
#define PT_CAT(a, b) PT_CAT_(a, b)
/* pt_<name><PT_SUFFIX>. */
#define PT_FN(name) PT_CAT(PT_CAT(pt_, name), PT_SUFFIX)
/* A float from its bits, and an exact integer convert (callers pass values < 2^24). */
#define PT_K(bits) pt_from_bits(bits)
#define PT_FROM_U32(u) ((float)(uint32_t)(u))

#endif
