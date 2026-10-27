# @ajax/wasm-decode

Decodes a WebAssembly binary into a `wasm_module` (`@ajax/wasm-module`), checking everything the binary
format requires: the header, section ids, order and exact sizes, custom sections' names, every vector
and LEB, names' UTF-8, value types, limits flags (shared and 64-bit ones are recorded for the
validator), element and data segment kinds, the function/code and data-count agreements, local counts
(at most 50 000, as wasmparser), and well-formed constant expressions. Function bodies stay byte ranges
for `@ajax/wasm-validate`. Counts are checked against the bytes left before anything is allocated, so a
hostile binary never makes it allocate much or read out of bounds. Messages follow wasmparser's.

Part of alpha-c-mesh's WebAssembly interpreter, which replaces wasmtime and wasmparser in the port of
alpha-rust-mesh's `mesh-host/src/wasm.rs`.

Tests: `tests/decode.c` (a module of every section kind, every truncation of it, and the malformed
binaries the format forbids); fuzzing in `@ajax/wasm-interp`'s tests.
