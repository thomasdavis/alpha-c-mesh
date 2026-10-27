# @ajax/wasm-reader

A cursor over a WebAssembly binary: bounds-checked reads of bytes, LEB128 numbers, little-endian
constants, names (with the spec's UTF-8 check) and vector counts (refused when they can't fit in what's
left, so a hostile count never allocates). The first failure is recorded as `"<what> (at offset 0x<n>)"`,
as wasmparser words it, and later reads return zeroes, so a parser reads straight through and checks
`failed` at the end. Header-only.

Part of alpha-c-mesh's WebAssembly interpreter (replacing wasmparser in the port of alpha-rust-mesh's
`mesh-host/src/wasm.rs`). Tests: through `@ajax/wasm-decode` and `@ajax/wasm-instr`.
