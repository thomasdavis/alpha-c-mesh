# @ajax/wasm-opcodes

The WebAssembly instruction set as data. For every instruction the decoder knows — the MVP, sign
extension, non-trapping float-to-int, multi-value, reference types and bulk memory as rows; SIMD,
relaxed SIMD and atomics by rule — `wasm_op_info` gives its immediates, its operand signature when it
is a plain one (`"ff>f"`), the proposal that introduced it, the access size of a load or store, and
wasmparser's name for it (its `Operator` variant: `F32Add`), which meshd's float guard prints.

SIMD and atomics are known only by their immediates (named `Simd`, `RelaxedSimd`, `Atomic`): enough for
the decoder and the float guard to walk and refuse them, not to run them.

Part of [alpha-c-mesh](https://github.com/thomasdavis/alpha-c-mesh)'s WebAssembly interpreter (it replaces
wasmparser in the port of alpha-rust-mesh's `mesh-host/src/wasm.rs`).

Tests: `tests/opcodes.c`.
