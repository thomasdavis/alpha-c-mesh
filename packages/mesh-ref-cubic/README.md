# @ajax/mesh-ref-cubic

programs/train's and programs/infer's CPU reference, written operation for operation as the programs
compute in the cluster float mode: a cubic `p0 + p1·x + p2·x² + p3·x³` fitted by SGD to `sin(πx)` sampled
on a 256-point grid. Each step, lane l of a warp takes sample `(8l + off) mod 256`, and the loss and the
four gradient sums are warp butterflies; `apply` is the SGD update and `loss` the value a step records.
`sin_pi` is train's own (an f64 Taylor polynomial after folding into [−½, ½], with flushing converts).

**Ported from** `crates/mesh-ref/src/lib.rs` (`cubic_z`, `cubic`, `warp_sum`, `step_sums`, `apply`,
`loss`, `sin_pi`, `grid`, `samples`). Freestanding.

**Tested** by a golden digest from the Rust: the samples, a 64-point grid, `sin_pi` over 200 000 inputs,
300 SGD steps (every sum, loss and parameter, and the cubic at a point) and a warp sum.

## Differences

- `grid` and `samples` write caller buffers instead of returning `Vec`s; `cubic` takes a float (Rust has
  `cubic_z` on `Z` and `cubic` on `f32`, which are the same here).
