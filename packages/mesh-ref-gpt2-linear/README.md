# @ajax/mesh-ref-gpt2-linear

A GPT-2 linear layer's output row in the order the GPU's GEMM tile and GEMV give exactly: each output is
the sum over k ascending from +0 of x[k]·w[k][n] in the cluster float mode, then the epilogue — `+ b`,
GELU of it, or a residual add into the output in place. Weights are `[in][out]` as the checkpoint stores
them. `project_cols` computes a column range (a thread's share of a row).

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`Epi`, `project_row`, `project_cols`). Freestanding.
Compiled with `-O2` in every profile: a forward pass is billions of these operations.

**Tested** in `@ajax/mesh-ref-gpt2`'s golden data (each epilogue, whole rows and a column range).
