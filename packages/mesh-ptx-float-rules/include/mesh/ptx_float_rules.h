/* @ajax/mesh-ptx-float-rules: the cluster float mode's rules on PTX text: refuses a program
 * whose float instructions are not correctly rounded, unfused and exact, or
 * whose f32 instructions lack .ftz where they have it, so CUDA and Metal
 * members of a cluster compute the same bits.
 *
 *   add sub mul div rcp sqrt   .rn, and .ftz on f32 (f64: .rn; PTX has no f64 ftz)
 *   abs neg min max setp set slct   .ftz on f32
 *   cvt   .ftz when the source is f32, or f64 -> f32
 *   selp copysign testp mov ld ldu st   no rounding, no flush: allowed as they are
 * Refused outright: .approx, div.full, fma / mad, f16 / bf16 / tf32 / fp8
 * arithmetic, and any other float instruction (no rule for it here).
 * Ported line for line from alpha-rust-mesh scripts/ptx-float-check.sh (awk). */
#ifndef MESH_PTX_FLOAT_RULES_H
#define MESH_PTX_FLOAT_RULES_H

#include <stddef.h>
#include <stdio.h>

/* Why one line breaks the rules, or NULL when it is fine (or not a float
 * instruction). The reasons are the script's words. */
const char *mesh_ptx_float_line(const char *line, size_t len);

/* Checks a whole PTX text (path names it in messages). Prints up to 5
 * "path:line: why: the line" problems to out (as the script) and returns how
 * many it found (it stops counting at 5). */
int mesh_ptx_float_check(const char *path, const char *text, size_t len, FILE *out);

#endif
