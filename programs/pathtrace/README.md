# @ajax/mesh-prog-pathtrace

A deterministic path tracer as a job program: one work item (a 32 x 24 tile, one pass) a task; the
node renders the pass's pixels into shared memory (every thread), warp 0 folds them into the tile's
accumulators in pass order and tone maps them. The image is bit for bit the same on every backend
because every float op is correctly rounded, unfused and FTZ, and the random numbers are a hash. The
renderer is the pathtrace-* packages, instantiated here for the device with plain f32 operations.
`variants/` (pathtrace-live, pathtrace-cpu) run the same images.

- **Ported from:** alpha-rust-mesh `programs/pathtrace/src/lib.rs`.
- **Tests:** in @ajax/mesh-prog-pathtrace-check (the reference is host-only): kcheck's PT checks on the
  simulator against @ajax/mesh-ref-pathtrace, bitwise.
