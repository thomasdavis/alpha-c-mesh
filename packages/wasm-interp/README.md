# @ajax/wasm-interp

A WebAssembly interpreter in C, the engine of alpha-c-mesh's wasm executor (it replaces wasmtime in the
port of alpha-rust-mesh's `mesh-host/src/wasm.rs`).

- `wasm_compile`: decode (`@ajax/wasm-decode`), validate (`@ajax/wasm-validate`), lower
  (`@ajax/wasm-lower`). The result is immutable and shared by every thread.
- `wasm_instantiate`: host imports resolved by module, name and signature (wasmtime's messages), store
  limits as wasmtime's `StoreLimits` (memory bytes: past them `memory.grow` returns -1 and a larger
  initial memory fails; tables; memories), globals, tables, element and data segments, the start
  function. One instance per thread.
- `wasm_call`: runs a function on an explicit value stack and frame stack (no C recursion), returning
  WASM_NUM_OK or a trap code numbered as wasmtime's `Trap` (meshd reports it in FAULT_TRAP).

The engine is meshd's: every arithmetic NaN is canonical (`@ajax/wasm-numeric`, as
`cranelift_nan_canonicalization(true)`), no signal handlers (every memory and table access is
bounds-checked), and epoch interruption: given an epoch counter and a deadline, a call traps with
`interrupt` at the next loop header or function entry once the counter reaches the deadline.

Proposals run: the MVP, mutable globals, sign extension, saturating float-to-int, multi-value,
reference types, bulk memory, extended constant expressions (`WASM_F_INTERP`) — what clang's wasm32
output uses. Not run: SIMD, relaxed SIMD, threads, multi-memory, memory64 (refused at compile).

## Differences from wasmtime

- An interpreter, not a JIT: much slower per instruction (see the executor's README for task rates).
- No SIMD (wasmtime runs it for images declaring `float = "ieee"`).
- The call stack is bounded by frames and value slots, not native stack bytes.

## Tests

- `tests/smoke.c`: arithmetic, recursion, a loop, memory.
- `tests/spec.c`: every trap with its code, memory and table bounds, call_indirect, br_table,
  multi-value, select, globals, bulk memory, table ops, canonical NaNs, imports and host traps, store
  limits, interrupts, the start function, SIMD refused.
- `tests/differential.c`: a C program (`tests/fixtures/prog.c`) compiled natively and to wasm32 by
  clang (bytes in `fixtures/prog.h`) gives the same numbers for 40 seeds, integer and float.
- `tests/fuzz.c`: 9000 mutated and random modules; none crash (run with `--profile asan`).
