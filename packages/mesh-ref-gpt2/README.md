# @ajax/mesh-ref-gpt2

GPT-2 124M in the cluster float mode on the CPU: the CPU reference of programs/gpt2x (the exact-mode GPU
kernels) and the exact executor of programs/gpt2 on a CPU. Every value is a fixed function of the weights
and the token prefix — the order of every sum is fixed and the GPU kernels follow it operation for
operation — so the logits and the KV cache are bitwise the same on every executor, and a row computed
alone (decode) equals the same row computed in a batch (prefill).

`mesh_gpt2_forward` runs rows through the model and appends their K and V to the cache;
`mesh_gpt2_forward_layers` calls back after each layer (a prefill can stream its KV); `mesh_gpt2_generate`
is greedy generation with one digest over every step's logits. Work is split over POSIX threads by
(row, column block), by row and by vocabulary row; the results never depend on the thread count.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`forward`, `forward_layers`, `generate`, `par_linear`,
`par_rows`, `project_vocab`). Host only.

**Tested** on synthetic weights against golden data from the Rust: the row operations on hashed data,
the weights, a greedy generation (tokens, logits digest, KV digest), and gpt2.rs's
`decode_rows_equal_prefill_rows` — three tokens one at a time and all at once give the same KV and
logits, both equal to the Rust's.

## Differences

- Threads come from a shared counter over independent units rather than Rust's scoped chunking; outputs
  go to caller buffers; out-of-memory and context overflow return false instead of panicking.
