/* @ajax/pathtrace-f32: programs/pathtrace's renderer with plain f32 operations (pathtrace-core's
 * `impl Real for f32`): pt_sample_f32, pt_tile_pass_f32, pt_tile_fold_f32, ... (pathtrace/declare.h
 * lists them). On a CPU this is IEEE f32 with subnormals kept; the GPU build is the same code with
 * every op .ftz. Freestanding. */
#ifndef PATHTRACE_F32_H
#define PATHTRACE_F32_H

#define PT_SUFFIX _f32
#include "pathtrace/declare.h"
#undef PT_SUFFIX

#endif
