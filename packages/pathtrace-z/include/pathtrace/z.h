/* @ajax/pathtrace-z: programs/pathtrace's renderer in the cluster float mode Z (mesh-ref's
 * `impl Real for Z`, every op through @ajax/mesh-ftz): pt_sample_z, pt_tile_pass_z, pt_tile_fold_z,
 * ... (pathtrace/declare.h lists them). The CPU reference and the wasm image's renderer: a tile here is
 * bit for bit the tile every backend must render. Freestanding. */
#ifndef PATHTRACE_Z_H
#define PATHTRACE_Z_H

#define PT_SUFFIX _z
#include "pathtrace/declare.h"
#undef PT_SUFFIX

#endif
