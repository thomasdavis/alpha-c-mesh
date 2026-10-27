# @ajax/wasm-types

The WebAssembly binary format's constants (spec §5.3–5.5): value types by their encoding, section ids,
import/export kinds, limits flags, the page size, and the feature bits (one per proposal past the MVP)
the decoder, validator and interpreter are configured with. `WASM_F_ALL` is everything this
implementation decodes (wasmparser's `WasmFeatures::all()` for those proposals); `WASM_F_INTERP` is what
the interpreter runs. Header-only and freestanding.

Part of [alpha-c-mesh](https://github.com/thomasdavis/alpha-c-mesh)'s WebAssembly interpreter (it replaces
wasmtime and wasmparser in the port of alpha-rust-mesh's `mesh-host/src/wasm.rs`).

Tests: through its users (`wasm-decode`, `wasm-validate`).
