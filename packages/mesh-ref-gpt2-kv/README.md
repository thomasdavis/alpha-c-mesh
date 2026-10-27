# @ajax/mesh-ref-gpt2-kv

GPT-2's KV cache: per layer, the K and V rows (768 wide, heads concatenated) of every position so far,
and its digest (FNV-1a 64 over each layer's K then V rows). Rows are only appended; during a forward pass
layer l's rows land before layer l + 1's, and `mesh_gpt2_kv_len` reports layer 0's count, as the Rust
`Kv::len` does.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`Kv`). Host only. **Tested** through the generation and
prefill/decode golden data of `@ajax/mesh-ref-gpt2`.

## Differences

- `Vec<Vec<f32>>` per layer becomes one growable buffer per layer with a shared capacity and per-layer
  row counts.
