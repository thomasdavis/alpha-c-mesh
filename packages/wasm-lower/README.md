# @ajax/wasm-lower

Lowers validated WebAssembly function bodies to the flat code `@ajax/wasm-interp` runs. Structured
control flow becomes jumps: every branch names its target instruction, the operand-stack height (in
64-bit slots from the frame base) its label's values move to, and how many values move; `br_table`
entries live in a side table. Locals are the frame's first slots, the operand stack follows, and each
function records the deepest stack it can reach, so a call checks its stack space once. Loop headers
carry an interrupt check (wasmtime's epoch checks sit at loop headers and function entries). Dead code
after `br`, `br_table`, `return` and `unreachable` is dropped. Bodies with SIMD or atomics are refused
("SIMD is not supported by this executor"). `wasm_canonical_types` gives call_indirect's structural
type identity.

Part of alpha-c-mesh's WebAssembly interpreter, which replaces wasmtime's Cranelift in the port of
alpha-rust-mesh's `mesh-host/src/wasm.rs`.

Tests: through `@ajax/wasm-interp` (spec-style, differential against native code, and fuzzing).
