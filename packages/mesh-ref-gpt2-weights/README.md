# @ajax/mesh-ref-gpt2-weights

Synthetic GPT-2 124M weights from a counter hash, for checks without a checkpoint: each tensor gets its
own seed (`seed · 0x9e3779b9 + tensor index`) and `@ajax/mesh-ref-matmul`'s values scaled — matrices to
[−0.05, 0.05), biases to [−0.01, 0.01), layer-norm gains to 1 ± 0.1 and their biases to ±0.1.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`synthetic`). Host only (allocates ~500 MB).
**Tested** by a sampled golden digest of the whole buffer from the Rust (in `@ajax/mesh-ref-gpt2`).
