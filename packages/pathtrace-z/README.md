# @ajax/pathtrace-z

programs/pathtrace's renderer in the cluster float mode: every operation through `@ajax/mesh-ftz`
(`pt_sample_z`, `pt_tile_pass_z`, …). This is the CPU reference and the wasm image's renderer: a tile
rendered with it is bit for bit the tile every backend must render.

**Ported from** mesh-ref's `impl Real for Z` (`crates/mesh-ref/src/pathtrace.rs`). Freestanding.

**Tested** with golden digests from the Rust's Z instance (tiles, scalars, samples, as in
`@ajax/pathtrace-f32`). The tile digest is the same as the f32 instance's, which is the Rust's
`f32_and_z_agree` test: no value of the default scene is subnormal.
