# @ajax/wasm-float-guard

meshd's load-time check of a wasm CPU program, ported from `float_check` in alpha-rust-mesh's
`mesh-host/src/wasm.rs`:

1. Valid under every proposal this implementation knows, else `jit: invalid wasm: …`.
2. Valid without threads, shared memory and relaxed SIMD — and, for an image declaring the cluster
   float mode (`float = "ftz"`, the default, required for lockstep), without any SIMD — else
   `float: … (a CPU program uses no threads, shared memory or relaxed SIMD; an FTZ image no SIMD)`.
3. Imports only `mesh` functions, else `load: import m::n: a CPU program imports only `mesh`
   functions (mesh-abi v2::wasm)`.
4. In an FTZ image every flushing f32 op (arithmetic, compares, f32↔f64 converts: the same 22 as the
   Rust's `flushing_f32`) is inside an exported `mesh_ftz_*` helper (`@ajax/mesh-ftz`'s wasm32 exports),
   else `float: F32Add in function 0, outside the image's mesh_ftz_* helpers: …`, naming the op as
   wasmparser's `Operator` Debug output does.

It reports functions, helpers and f32 ops, as `FloatReport`.

## Differences

wasmparser type-checks SIMD bodies in step 1; `@ajax/wasm-validate` walks them without typing (see its
README). An IEEE image with ill-typed SIMD therefore passes this guard and is then refused by the
interpreter (`SIMD is not supported by this executor`), also a `jit` reason.

Tests: `tests/float_guard.c` (the Rust's `float_guard` test case for case, plus threads and the op list).
