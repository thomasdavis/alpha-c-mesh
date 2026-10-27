# @ajax/mesh-ref-pathtrace

programs/pathtrace's CPU reference: the renderer in the cluster float mode (`@ajax/pathtrace-z`, the same
code the wasm image runs), so a tile rendered here is bit for bit the tile every backend must render.
`mesh_pt_tile_render` renders one tile pass by pass (each pass's sums from +0, folded in pass order) and
keeps every digest kcheck compares; `mesh_pt_tiles` renders many on every core; `mesh_pt_raster` and
`mesh_pt_ppm` assemble the image.

**Ported from** `crates/mesh-ref/src/pathtrace.rs`. Host only (POSIX threads).

**Tested** by golden data from the Rust: a 64×48 image of the default scene (2 spp, 3 passes) under both
tone maps — every tile's pass, accumulator and pixel digests, and the PPM's bytes.

## Differences

- Rust's `Tile` holds `Vec`s; here the accumulators and pixels are fixed arrays and the pass digests are
  malloc'd (`mesh_pt_tile_free`). Out-of-memory returns false instead of aborting.
- The Rust test `f32_and_z_agree` is covered by `@ajax/pathtrace-f32` and `-z` both matching the same Rust
  tile digest.
