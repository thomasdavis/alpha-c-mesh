# @ajax/mesh-prog-train

SGD of y = a + b x + c x^2 + d x^3, one sample a lane, gradients butterfly-summed across the warp; in a
lockstep group of N each step's sums go through the exchange page and every member applies
slot0 + .. + slot(N-1) in rank order, so the params stay bitwise identical. OP_INIT generates the
samples with sin(pi x) in f64 (only + - * /). Layout: @ajax/mesh-abi-view-train.

- **Ported from:** alpha-rust-mesh `programs/train/src/lib.rs`.
- **Tests:** on the simulator against @ajax/mesh-ref-cubic, bitwise: the samples (and `sin_pi` itself),
  six tasks of 64 steps with every param and loss, OP_SET_LR; then a lockstep pair (two mailboxes, a
  thread mirroring the exchange pages as meshd's peer relay does) whose members end bitwise identical
  and equal to the reference of a 64-sample batch.
