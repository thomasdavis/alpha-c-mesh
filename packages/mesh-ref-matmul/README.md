# @ajax/mesh-ref-matmul

programs/matmul's exact mode on the CPU, the seed of GPU inference: f32 matrices from a counter hash,
`C = A B` with every element a sequential sum over k from +0.0 (a correctly rounded multiply, then a
correctly rounded add, both in the cluster float mode, never fused), and `y = A x` in the GPU's
warp-strided order (lane l sums k = l, l + 32, … then the warp butterfly). `mesh_matmul_digest` is the
FNV-1a 64 the checkers compare.

**Ported from** `crates/mesh-ref/src/matmul.rs`. Host only: `mesh_matmul` splits rows over POSIX threads
(the result is the same for any thread count).

**Tested** by matmul.rs's test, ported, and a golden digest from the Rust over matmul and matvec at four
shapes plus a thousand matrix values.

## Differences

- Rust uses scoped threads; here POSIX threads, and if a thread can't be started its rows run on the
  caller's thread. Outputs go to a caller buffer instead of a returned `Vec`.
