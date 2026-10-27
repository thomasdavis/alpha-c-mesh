# @ajax/wasm-leb128

LEB128 decoding exactly as the WebAssembly binary format defines it (spec §5.2.2): unsigned and
signed readers for any width up to 64 bits (u32, u64, s32, s33 for block types, s64). Each returns the
bytes it used, or 0 for a truncated encoding, one longer than ceil(N/7) bytes, or a last byte whose
unused bits aren't zero (unsigned) or the sign (signed). Header-only and freestanding.

Part of [alpha-c-mesh](https://github.com/thomasdavis/alpha-c-mesh)'s WebAssembly interpreter, which
replaces wasmtime in the port of alpha-rust-mesh's `mesh-host/src/wasm.rs` (wasmparser did this there).

Tests: `tests/leb128.c` (edge values, the length and padding rules, round trips).
