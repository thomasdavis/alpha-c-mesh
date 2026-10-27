# @ajax/mesh-prog-blocks

The block collectives of a multi-warp node, for kcheck: every thread computes every reduction, scan,
a broadcast and a shared-memory round trip on @ajax/mesh-ref-block's test values (zeros, subnormals,
large values); op 2 lets warps 1.. leave at once while warp 0 runs a warp sum. `variants/blocks-w4` is
the same image with 4 warps a node.

- **Ported from:** alpha-rust-mesh `programs/blocks/src/lib.rs`.
- **Tests:** kcheck's B1, B2 and B3 on the simulator: 32-, 4- and 1-warp nodes, six seeds, every header
  and per-thread word bitwise equal to @ajax/mesh-ref-block (subnormals included: the simulator's
  flush-to-zero matches the cluster float mode); the 4-warp f32 sum equals the 1024-leaf tree's; 64
  early exits, a stop/start, and a task after it.
