/* @ajax/mesh-ref-gpt2-norm: layer normalization of a row of WIDTH, in the node's order (mesh-ref
 * gpt2.rs `normalize_row`): thread t sums x[t], x[t + 256], x[t + 512] from +0, then the block tree;
 * the same for the squared deviations; ((x - mean) / sqrt(var + eps)) g + b. Freestanding. */
#ifndef MESH_REF_GPT2_NORM_H
#define MESH_REF_GPT2_NORM_H

void mesh_gpt2_normalize_row(const float *x, const float *g, const float *b, float *out);

#endif
