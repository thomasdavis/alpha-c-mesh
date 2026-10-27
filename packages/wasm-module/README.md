# @ajax/wasm-module

A decoded WebAssembly module as plain C structs: types, imports, functions, tables, memories, globals,
exports, the start function, element and data segments, and code bodies. Everything points into the
binary it was decoded from (which must outlive it); only the arrays are owned (`wasm_module_free`). Index
spaces put imports first, as the spec does.

Produced by `@ajax/wasm-decode`, checked by `@ajax/wasm-validate`, run by `@ajax/wasm-interp`: the
pieces of alpha-c-mesh's replacement for wasmtime in the port of alpha-rust-mesh's `mesh-host/src/wasm.rs`.

Tests: through `@ajax/wasm-decode`.
