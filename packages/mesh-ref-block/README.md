# @ajax/mesh-ref-block

The block collectives of a multi-warp node, on the CPU, step for step as the GPU computes them: the warp
butterfly (`lane l := op(own, lane l ^ m)` for m = 16 … 1), the warp's inclusive Hillis–Steele scan, and
the block reduce and scan built from them (each warp, then the warp totals, then the prefixes). Every f32
operation is in the cluster float mode (`@ajax/mesh-ftz`), so for thread values `vals` a GPU's result is
bitwise the one these return.

```c
float s = mesh_block_reduce_f32(vals, 256, mesh_block_sum_f32_id(), mesh_block_add_f32);
mesh_block_scan_u32(vals, 256, 0, mesh_block_add_u32, true, out);
```

Also `mesh_block_mix` (the lowbias32 hash every backend shares), `mesh_block_max_f32` (the IEEE total
order of the bits) and `mesh_block_test_values` (what programs/blocks gives each thread).

**Ported from** `crates/mesh-ref/src/block.rs`. Freestanding (no libc).

**Tested** by block.rs's test, ported, and a golden digest from the Rust over every collective (f32 sum
and max, u32 and u64 sums, inclusive and exclusive scans) for 20 seeds at 32 to 1024 threads.

## Differences

- Rust's functions are generic over `T` with a closure; here there are `_f32`, `_u32` and `_u64` versions
  taking a function pointer.
- Rust panics when the thread count isn't a multiple of 32 in 32..1024. Here `reduce` returns `id` and
  `scan` returns false (leaving `out` alone).
