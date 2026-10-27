# @ajax/pathtrace-scene

programs/pathtrace's scene format. A scene is u32 words (floats as their bits): a 32-word header (magic
"PTSC", version, image size, samples per pass, passes, depth, seed, record counts, the light, exposure,
clamp, length, the pinhole camera, the mesh's bounding sphere), then materials, quads, spheres and
triangles. `pt_layout_of` finds each part, `pt_validate` checks every index and size (returning the
Rust's error codes 1–10), and `pt_set_param` applies a fleet job's `OP_SET` to one of the six settable
words. The image is cut into 32×24 tiles (`pt_tiles`, `pt_tile_origin`).

**Ported from** `crates/pathtrace-core/src/lib.rs` (the scene constants, `Layout`, `layout`, `validate`,
`validate_in`, `SETTABLE`, `set_arg`, `set_param`, `tiles`, `tiles_x`, `tile_origin`). Freestanding.

**Tested** in `@ajax/pathtrace-job-scene`'s tests (they need the default scene): every validation rule
on a mutated default scene, with the codes the Rust returns, and the settable words.

## Differences

- `validate` takes the slice's length explicitly, and computes the tile count in 64 bits: a header with an
  absurd width × height that overflows u32 is rejected here, where a Rust release build would wrap.
