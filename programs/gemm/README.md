# @ajax/mesh-prog-gemm

f32 matrix multiply throughput as a native worker program: a cache-blocked GEMM (4 x 16 register
tiles of fused multiply-adds, k blocks of 256, j blocks of 512) over every CPU, timed tasks, job items
with their records, a check op writing C to the view, the plain loop and a one-thread run.

- **Ported from:** alpha-rust-mesh `programs/gemm/src/main.rs` (its Linux path).
- **Differences:** the macOS Accelerate (`cblas_sgemm`, AMX) path is not ported, so `impl` is always 2. The
  thread count is the online CPUs, not the cgroup quota (Rust's `available_parallelism`).
- **Tests:** the kernel and the plain loop against mesh-ref's f64 reference on sizes off the tile grid;
  on @ajax/mesh-worker-node's loop: OP_CHECK's C, sizes, a timed run, a job record, the plain and
  one-thread runs, threads.
