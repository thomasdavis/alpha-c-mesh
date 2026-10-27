# @ajax/pathtrace-job-scene

A fleet job's scene: the default scene with the job's setup tasks applied in order (`OP_DEFAULT` loads
the default again; `OP_SET` sets one settable word), exactly as every member's program applies them to its
view. Returns the index of the first task that is neither.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`job_scene`). Freestanding.

**Tested** with a golden digest of a job scene from the Rust and its error indices; the tests here also
cover `@ajax/pathtrace-scene`'s validation (every rule) and the Rust's `default_scene_is_valid`.

## Differences

- Rust returns the scene by value or `Err(i)`; here the scene goes to a caller buffer and the result is
  -1 or i.
