# @ajax/wasm-validate

Validates a decoded WebAssembly module (spec chapter 3) for a chosen set of proposals: the module's
index spaces, limits, constant expressions (with extended constant expressions), exports (unique
names), the start function, element and data segments, declared function references, and a type check
of every function body with the spec's algorithm — an operand stack of types and a stack of control
frames, polymorphic after `unreachable`, `br`, `br_table` and `return`. Instructions of a proposal that
isn't enabled are refused as wasmparser refuses them (`SIMD support is not enabled (at offset 0x1f)`).

SIMD instructions are decoded but not type-checked: where SIMD is enabled, a body's type check stops at
its first SIMD instruction and the rest is walked for structure and disabled proposals only, and the
report says SIMD is used. The interpreter (`@ajax/wasm-interp`) runs no SIMD, so such a function never
runs; meshd's float guard needs only to know whether SIMD, threads or relaxed SIMD appear, which this
gives exactly.

Part of alpha-c-mesh's WebAssembly interpreter, which replaces wasmtime and wasmparser in the port of
alpha-rust-mesh's `mesh-host/src/wasm.rs`.

## Differences

- wasmparser type-checks SIMD bodies; this doesn't (above).
- Messages follow wasmparser's wording where it matters to callers (the proposal refusals, which
  meshd's float guard reports); others are close but not always identical.

Tests: `tests/validate.c` (valid bodies of every instruction kind; type, index and structure errors;
each disabled proposal); clang-built guest programs in the wasm executor's tests.
