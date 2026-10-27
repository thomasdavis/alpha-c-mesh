# @ajax/mesh-prog-infer

The trained cubic over a 64-point grid, read from train's params through a peer view (manifest
`reads = ["train"]`). Layout: @ajax/mesh-abi-view-infer.

- **Ported from:** alpha-rust-mesh `programs/infer/src/lib.rs`.
- **Tests:** on the simulator against @ajax/mesh-ref-cubic, bitwise: the grid after init, no train ->
  R_NO_TRAIN, every point's y for given params, the probe op on a readable and an unreadable slot (with
  FAULT_PEER_VIEW), and an unloaded train.
