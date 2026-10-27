# @ajax/mesh-rt-linalg

Dense linear algebra at node scope in the exact mode: `mesh_linalg_gemm_tile` (a 64 x 64 tile of C on a
256-thread node, k in slabs of 16 through shared memory) and `mesh_linalg_gemv_cols` (64 columns of one
row, slabs of B staged through registers into shared memory). Every output is the sequential sum over k
from +0.0 of a correctly rounded multiply and add, never fused, so the bits do not depend on tile shape,
node or GPU.

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/linalg.rs`.
- **Tests:** programs/matmul compares its tiles and rows with @ajax/mesh-ref-matmul on the simulator.
