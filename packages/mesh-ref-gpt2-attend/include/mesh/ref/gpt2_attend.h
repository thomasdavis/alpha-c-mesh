/* @ajax/mesh-ref-gpt2-attend: one head's attention for one query (mesh-ref gpt2.rs `attend_row`), in
 * the node's order: thread t scores keys j = t, t + 256, .. (each a sequential dot over 64 from +0,
 * times 1/8), max and sum by block trees, p_j = e_j / sum; output lane d of group g = 0..4 sums
 * p_j v_j[d] over j = g, g + 4, .. from +0, and the four groups are added in order. Freestanding. */
#ifndef MESH_REF_GPT2_ATTEND_H
#define MESH_REF_GPT2_ATTEND_H

#include <stdbool.h>
#include <stddef.h>

/* q: HEAD_DIM values; key j is k + j * k_stride, value j is v + j * v_stride (HEAD_DIM each), j < n;
 * out: HEAD_DIM. False (out untouched) when n > MESH_GPT2_CTX. */
bool mesh_gpt2_attend_row(const float *q, size_t n, const float *k, size_t k_stride, const float *v,
                          size_t v_stride, float *out);

#endif
