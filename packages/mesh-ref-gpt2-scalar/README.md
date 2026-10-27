# @ajax/mesh-ref-gpt2-scalar

GPT-2's scalar functions built only from `+ - * /` and integer bit operations, every float operation in
the cluster float mode, so every executor (CPU, CUDA, Metal, wasm) computes the same bits: `exp` (range
reduction by k·ln 2 in two parts, a degree-10 Horner polynomial, 2^k in two exact steps), `tanh` from it,
the tanh-form GELU, and a Newton square root. The constants are f32 bit patterns.

**Ported from** `crates/mesh-ref/src/gpt2.rs` (`exp_poly`, `tanh_poly`, `gelu_tanh`, `sqrt_newton`, the
constants). Freestanding.

**Tested** by gpt2.rs's `constants_are_their_values` and `scalar_functions_are_close` (the double
references computed without libm), and a golden digest from the Rust over 300 000 inputs of each
function (random bits, tiny exponents, ±150).

## Differences

- `exp_poly` of a NaN: Rust's `as i32` turns NaN into 0, where a C cast is undefined, so it is tested
  first and mapped to 0 as well.
