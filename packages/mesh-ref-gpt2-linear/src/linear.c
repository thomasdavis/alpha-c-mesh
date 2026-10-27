/* A linear layer's output row (mesh-ref gpt2.rs `project_row`, `project_cols`). */
#include "mesh/ref/gpt2_linear.h"

#include "mesh/ftz.h"
#include "mesh/ref/gpt2_scalar.h"

void mesh_gpt2_project_row(const float *x, size_t kd, const float *w, const float *b, size_t nout,
                           mesh_gpt2_epi epi, float *out) {
    mesh_gpt2_project_cols(x, kd, w, b, nout, epi, 0, nout, out);
}

void mesh_gpt2_project_cols(const float *x, size_t kd, const float *w, const float *b, size_t nout,
                            mesh_gpt2_epi epi, size_t c0, size_t ncols, float *out) {
    for (size_t j = 0; j < ncols; j++) {
        size_t n = c0 + j;
        float acc = 0.0f;
        for (size_t k = 0; k < kd; k++) acc = mesh_ftz_add(acc, mesh_ftz_mul(x[k], w[k * nout + n]));
        float v = mesh_ftz_add(acc, b[n]);
        switch (epi) {
        case MESH_GPT2_EPI_BIAS: out[j] = v; break;
        case MESH_GPT2_EPI_BIAS_GELU: out[j] = mesh_gpt2_gelu_tanh(v); break;
        case MESH_GPT2_EPI_BIAS_RESIDUAL: out[j] = mesh_ftz_add(out[j], v); break;
        }
    }
}
