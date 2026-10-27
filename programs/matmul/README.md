# @ajax/mesh-prog-matmul

The exact-mode GPU GEMM and GEMV: 64 x 64 tiles of C on 8-warp nodes (@ajax/mesh-rt-linalg), y = A x a
warp a row, the matrices generated on the device in the program's scratch and copied out through the
view's window. Every output a sequential sum in k from +0.0, unfused and FTZ, so the bits equal the CPU
reference on every GPU.

- **Ported from:** alpha-rust-mesh `programs/matmul/src/lib.rs`.
- **Tests:** in @ajax/mesh-prog-matmul-check (the reference is host-only): on the simulator against
  @ajax/mesh-ref-matmul, bitwise, for dimensions off the tile grid, and bad arguments and node shapes refused.
