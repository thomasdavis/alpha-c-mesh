# @ajax/mesh-ref-gpt2-shape

GPT-2 124M's shape — 12 layers, 12 heads of 64, width 768, feed-forward 3072, vocabulary 50 257, context
1024, the node's 256 threads — and the flat weight buffer's layout: every tensor in checkpoint order and
shape (`wte`, `wpe`, the twelve tensors of each block, `ln_f`), the same on the CPU and in the GPU's
scratch. `mesh_gpt2_tensor` gives each tensor's checkpoint name, offset and count.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (the constants, `BlockOff`, `Layout`, `Layout::tensors`).
Freestanding. **Tested** by gpt2.rs's `layout_covers_the_checkpoint` and a golden digest of every
tensor's name, offset and count from the Rust.

## Differences

- `tensors()` returns a `Vec<(String, usize, usize)>`; here `mesh_gpt2_tensor(i, ...)` fills a buffer.
