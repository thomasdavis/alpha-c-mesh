/* Layer normalization of a row (mesh-ref gpt2.rs `row_reduce`, `normalize_row`). */
#include "mesh/ref/gpt2_norm.h"

#include "mesh/ftz.h"
#include "mesh/ref/block.h"
#include "mesh/ref/gpt2_scalar.h"
#include "mesh/ref/gpt2_shape.h"

/* Thread t's sequential sum (from +0) over i = t, t + NT, .., then the block tree. sq: of (x - mean)^2. */
static float row_reduce(const float *x, int sq, float mean) {
    float parts[MESH_GPT2_NT];
    for (int t = 0; t < MESH_GPT2_NT; t++) {
        float s = 0.0f;
        for (int i = t; i < MESH_GPT2_WIDTH; i += MESH_GPT2_NT) {
            float f = x[i];
            if (sq) {
                float d = mesh_ftz_sub(x[i], mean);
                f = mesh_ftz_mul(d, d);
            }
            s = mesh_ftz_add(s, f);
        }
        parts[t] = s;
    }
    return mesh_block_reduce_f32(parts, MESH_GPT2_NT, mesh_block_sum_f32_id(), mesh_block_add_f32);
}

void mesh_gpt2_normalize_row(const float *x, const float *g, const float *b, float *out) {
    float mean = mesh_ftz_div(row_reduce(x, 0, 0.0f), (float)MESH_GPT2_WIDTH);
    float var = mesh_ftz_div(row_reduce(x, 1, mean), (float)MESH_GPT2_WIDTH);
    float inv = mesh_ftz_div(1.0f, mesh_gpt2_sqrt_newton(mesh_ftz_add(var, mesh_ftz_from_bits(MESH_GPT2_LN_EPS_BITS))));
    for (int i = 0; i < MESH_GPT2_WIDTH; i++)
        out[i] = mesh_ftz_add(mesh_ftz_mul(mesh_ftz_mul(mesh_ftz_sub(x[i], mean), inv), g[i]), b[i]);
}
