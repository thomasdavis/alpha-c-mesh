# @ajax/mesh-rt-prim

One include, `mesh/rt/prim.h`, for mesh-rt's device primitives on whichever target is compiled: inline
PTX on nvptx64 (@ajax/mesh-rt-ptx), the CPU simulator elsewhere (@ajax/mesh-rt-sim). Adds the
target-neutral f32 helpers: `mesh_f32_bits`/`mesh_f32_from_bits`, `mesh_rt_rdf`/`wrf`, `mesh_rt_shfl_idx_f`
and `mesh_rt_warp_sum` (the butterfly sum: bitwise the same on every lane).

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/ptx.rs` (`rdf`, `wrf`, `shfl_idx_f`, `warp_sum`).
- **Tests:** through every program's simulator tests and PTX builds.
