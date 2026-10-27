# @ajax/wasm-builder

WebAssembly binaries assembled from C: a byte buffer with the binary format's encoders (LEB128, names,
floats, sections, instruction shorthands), and a module builder for types, imports, functions with
locals and bodies, a memory, a table, globals, exports, a start function, and element and data
segments, written out as sections in the spec's order. Indices are the caller's to get right, so it
builds invalid modules on request — what tests of a validator need.

Written for the tests of alpha-c-mesh's WebAssembly interpreter (`wasm-decode`, `wasm-validate`,
`wasm-interp`, `wasm-float-guard`, the wasm executor), the way alpha-rust-mesh's `mesh-host/src/wasm.rs`
tests assemble modules by hand. Tests: `tests/builder.c` (byte-for-byte with wat2wasm's output).
