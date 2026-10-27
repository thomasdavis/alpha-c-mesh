# @ajax/mesh-prog-gpt2x

GPT-2 124M's exact-mode GPU kernels: the window (PUT, COPY), EMBED, NORM, GEMM and GEMV with their
epilogues (bias, GELU, residual), KV, ATTN and LOGITS, every op following the CPU reference operation
for operation (FTZ, correctly rounded, unfused, fixed sum orders; exp, tanh and sqrt from + - * /), so
the logits and the KV cache are bitwise the reference's on every GPU. The host runs the model as
stages of tasks. Layout: @ajax/mesh-abi-view-gpt2x.

- **Ported from:** alpha-rust-mesh `programs/gpt2x/src/lib.rs`.
- **Tests:** on the simulator, op by op against @ajax/mesh-ref-gpt2-*'s row operations, bitwise, on
  synthetic data: the scalar functions over a sweep, the window, EMBED, NORM, GEMM and GEMV with each
  epilogue off the tile grid, KV and causal ATTN for 12 heads, LOGITS for the first and last blocks.
