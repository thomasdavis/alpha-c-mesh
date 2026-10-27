/* @ajax/mesh-ref-gpt2-linear: a linear layer's output row in the order the GPU's GEMM tile and GEMV
 * give exactly (mesh-ref gpt2.rs `project_row`, `project_cols`): out[n] = (the sum over k ascending
 * from +0 of x[k] w[k][n]) then the epilogue. Weights [k][nout] (HF Conv1D). Freestanding. */
#ifndef MESH_REF_GPT2_LINEAR_H
#define MESH_REF_GPT2_LINEAR_H

#include <stddef.h>

/* How an output element is finished; acc is the sequential dot. */
typedef enum {
    MESH_GPT2_EPI_BIAS,         /* acc + b */
    MESH_GPT2_EPI_BIAS_GELU,    /* gelu(acc + b) */
    MESH_GPT2_EPI_BIAS_RESIDUAL /* out + (acc + b): a residual add in place */
} mesh_gpt2_epi;

/* out[n] for n in 0..nout from x[0..kd) and w (kd x nout); out is read for the residual. */
void mesh_gpt2_project_row(const float *x, size_t kd, const float *w, const float *b, size_t nout,
                           mesh_gpt2_epi epi, float *out);
/* project_row for the columns c0 .. c0 + ncols (out holds just those). */
void mesh_gpt2_project_cols(const float *x, size_t kd, const float *w, const float *b, size_t nout,
                            mesh_gpt2_epi epi, size_t c0, size_t ncols, float *out);

#endif
