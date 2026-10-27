# @ajax/mesh-ref-gpt2-norm

GPT-2's layer normalization of a row of 768, in the GPU node's order: thread t of 256 sums x[t],
x[t + 256], x[t + 512] from +0, then the block tree (`@ajax/mesh-ref-block`); the same for the squared
deviations; then `((x − mean) · 1/sqrt(var + eps)) · g + b`, all in the cluster float mode.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`row_reduce`, `normalize_row`). Freestanding.
**Tested** in `@ajax/mesh-ref-gpt2`'s golden data.
