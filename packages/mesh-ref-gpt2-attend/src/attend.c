/* One head's attention for one query (mesh-ref gpt2.rs `attend_row`). */
#include "mesh/ref/gpt2_attend.h"

#include "mesh/ftz.h"
#include "mesh/ref/block.h"
#include "mesh/ref/gpt2_scalar.h"
#include "mesh/ref/gpt2_shape.h"

bool mesh_gpt2_attend_row(const float *q, size_t n, const float *k, size_t k_stride, const float *v,
                          size_t v_stride, float *out) {
    if (n > MESH_GPT2_CTX) return false;
    float s[MESH_GPT2_CTX], e[MESH_GPT2_CTX], parts[MESH_GPT2_NT];
    for (size_t j = 0; j < n; j++) {
        const float *kj = k + j * k_stride;
        float a = 0.0f;
        for (int d = 0; d < MESH_GPT2_HEAD_DIM; d++) a = mesh_ftz_add(a, mesh_ftz_mul(q[d], kj[d]));
        s[j] = mesh_ftz_mul(a, mesh_ftz_from_bits(MESH_GPT2_ATTN_SCALE_BITS));
    }
    for (size_t t = 0; t < MESH_GPT2_NT; t++) {
        float m = mesh_block_max_f32_id();
        for (size_t j = t; j < n; j += MESH_GPT2_NT) m = mesh_block_max_f32(m, s[j]);
        parts[t] = m;
    }
    float m = mesh_block_reduce_f32(parts, MESH_GPT2_NT, mesh_block_max_f32_id(), mesh_block_max_f32);
    for (size_t j = 0; j < n; j++) e[j] = mesh_gpt2_exp_poly(mesh_ftz_sub(s[j], m));
    for (size_t t = 0; t < MESH_GPT2_NT; t++) {
        float a = 0.0f;
        for (size_t j = t; j < n; j += MESH_GPT2_NT) a = mesh_ftz_add(a, e[j]);
        parts[t] = a;
    }
    float sum = mesh_block_reduce_f32(parts, MESH_GPT2_NT, mesh_block_sum_f32_id(), mesh_block_add_f32);
    for (size_t j = 0; j < n; j++) e[j] = mesh_ftz_div(e[j], sum); /* e now holds p */
    for (int d = 0; d < MESH_GPT2_HEAD_DIM; d++) {
        float tot = 0.0f;
        for (size_t g = 0; g < MESH_GPT2_PV_GROUPS; g++) {
            float a = 0.0f;
            for (size_t j = g; j < n; j += MESH_GPT2_PV_GROUPS) a = mesh_ftz_add(a, mesh_ftz_mul(e[j], v[j * v_stride + d]));
            tot = g == 0 ? a : mesh_ftz_add(tot, a);
        }
        out[d] = tot;
    }
    return true;
}
