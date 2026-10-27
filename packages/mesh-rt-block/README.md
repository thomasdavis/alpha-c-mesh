# @ajax/mesh-rt-block

The block collectives of a multi-warp mesh node, in a fixed order that makes float results bitwise the
same on every GPU and in the CPU reference (`mesh_ref::block`): warp butterfly reductions, Hillis-Steele
warp scans, block reductions and scans over the 1024-leaf tree, broadcasts, and the fixed f32 operations
(sum with identity -0.0, max in the IEEE total order). Also `mesh_shared`, a typed, bounds-checked window
of the node's shared memory.

Rust's generic `Word` and closures become one function per type (`_u32`, `_f32`, `_u64`) taking the
operation as a function pointer: `mesh_block_reduce_f32(v, MESH_SUM_F32_ID, mesh_add_f32)`. Every call is
inlined, so the operation is too.

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/block.rs`.
- **Tests:** programs/blocks runs every collective on 1..32-warp nodes on the simulator and compares with
  the Rust reference's vectors (@ajax/mesh-ref-block).
