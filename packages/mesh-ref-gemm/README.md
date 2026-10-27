# @ajax/mesh-ref-gemm

The `gemm` native program's inputs and reference: `A[i][k] = ((7i + 13k) mod 17) / 8 - 1` and
`B[k][j] = ((5k + 11j) mod 19) / 9 - 1` (small exact fractions), `C = A B` summed in f64 with the
per-element error scale `Σ |a| |b|`, and `max_rel_err`, the bound an f32 GEMM is checked against (well
under 1e-5 at n = 64).

**Ported from** `crates/mesh-ref/src/lib.rs`, module `gemm`. Host (uses `fabs`, `malloc`; link `-lm`).

**Tested** by a golden digest from the Rust over the inputs, the reference sums and scales, and the
error, at n = 1, 7 and 64.

## Differences

- Rust returns `Vec`s; here the caller passes buffers (`max_rel_err` allocates its own and returns NaN if
  it can't).
