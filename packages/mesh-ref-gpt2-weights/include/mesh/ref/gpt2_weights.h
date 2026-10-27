/* @ajax/mesh-ref-gpt2-weights: synthetic GPT-2 124M weights from a counter hash (mesh-ref gpt2.rs
 * `synthetic`), for kcheck's GX checks without a checkpoint: matrices in [-0.05, 0.05), biases in
 * [-0.01, 0.01), layer-norm gains near 1. Host (allocates ~500 MB). */
#ifndef MESH_REF_GPT2_WEIGHTS_H
#define MESH_REF_GPT2_WEIGHTS_H

#include <stdint.h>

/* The whole flat buffer (mesh_gpt2_layout's total floats), malloc'd; NULL when memory runs out. */
float *mesh_gpt2_synthetic(uint32_t seed);

#endif
