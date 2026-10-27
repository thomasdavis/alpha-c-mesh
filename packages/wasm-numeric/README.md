# @ajax/wasm-numeric

WebAssembly's numeric instructions (spec §4.3) as C functions, deterministic across machines: integer
division and remainder with their traps, rotates and the arithmetic right shift without undefined
behaviour, bit counts, float min/max with the spec's NaN and signed-zero rules, `nearest` as
ties-to-even, trapping float-to-integer truncation with the exact bounds (NaN is "invalid conversion to
integer", out of range "integer overflow"), and the saturating ones.

As meshd's wasmtime engine is configured (`cranelift_nan_canonicalization(true)` in alpha-rust-mesh's
`mesh-host/src/wasm.rs`, so NaN bits are the same on x86 and arm), every NaN an arithmetic operation
produces is the canonical one (0x7fc00000 / 0x7ff8000000000000): add, sub, mul, div, sqrt, min, max,
ceil, floor, trunc, nearest, promote and demote. `neg`, `abs` and `copysign` are bit operations and keep
payloads. Trap codes are numbered as wasmtime's `Trap`, because meshd reports them in its FAULT_TRAP
word. Build users with `-ffp-contract=off` (no fused multiply-add). Header-only.

Tests: `tests/numeric.c`.
