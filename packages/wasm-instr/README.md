# @ajax/wasm-instr

Decodes one WebAssembly instruction at a time — the opcode, a prefixed sub-opcode, and its immediates
(block types, labels, br_table targets, memargs with multi-memory indices, constants, typed select,
reference types, bulk-memory and table indices, and the SIMD and atomic immediates) — from a function
body or a constant expression. It checks encodings only; typing is `@ajax/wasm-validate`'s. Messages
follow wasmparser's (`illegal opcode: 0x6 (at offset 0x0)`).

Part of alpha-c-mesh's WebAssembly interpreter (replacing wasmparser's operator reader in the port of
alpha-rust-mesh's `mesh-host/src/wasm.rs`). Tests: `tests/instr.c`.
