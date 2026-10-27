# @ajax/wasm-fuse

Superinstructions for `@ajax/wasm-interp`. Over a function's lowered code (`@ajax/wasm-lower`), an
operand-making instruction directly before a plain one becomes that instruction's operand:
`local.get A` or a constant before an arithmetic, compare, conversion, load or store, and
`local.get A; local.get B` as one instruction. In clang's -O2 wasm32 output `local.get` is about a quarter
of all instructions and constants a fifth, and the commonest pairs are exactly these. The interpreter
runs a fused instruction as its operand push plus a jump into the plain instruction's code, so each pair
costs one dispatch instead of two.

Nothing is fused across a branch target (a jump must land on a whole instruction), and every target —
branches, `if`, `br_table` entries — is remapped after the code shrinks.

Part of alpha-c-mesh's WebAssembly interpreter (it replaces wasmtime's JIT in the port of
alpha-rust-mesh's `mesh-host/src/wasm.rs`; see `@ajax/wasm-interp` for the speed this buys).

Tests: `tests/fuse.c` (what fuses, what doesn't, targets remapped); the interpreter's spec,
differential and fuzz tests run fused code.
