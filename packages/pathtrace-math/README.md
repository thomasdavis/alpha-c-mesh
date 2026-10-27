# @ajax/pathtrace-math

The float contract of programs/pathtrace's renderer and its math. In Rust, `pathtrace-core` is written once
over `R: Real` and instantiated with `f32` (the GPU) and mesh-ref's `Z` (CPUs). Here the renderer is
written once in template headers over macros — `PT_ADD`, `PT_SUB`, `PT_MUL`, `PT_DIV`, `PT_NEG`, `PT_LT`,
`PT_GT`, `PT_LE`, `PT_GE` and a name suffix `PT_SUFFIX` — and a translation unit instantiates it by
defining them and including `pathtrace/instantiate.h` (`@ajax/pathtrace-tile`). Values are plain `float`.

This package holds `pathtrace/real.h` (the contract), `pathtrace/consts.h` (the f32 bit constants,
`pt_v`, bit casts) and `pathtrace/t_math.h` (vectors with the Rust's exact operation order, `sqrt` by
three Newton steps from an integer guess, and `cis` by polynomial). Header only, freestanding; compile
instantiations with `-ffp-contract=off`.

**Ported from** `crates/pathtrace-core/src/lib.rs` (`Real`, the constants, `V` and its operations,
`sqrt`, `cis`). **Tested** in `@ajax/pathtrace-f32` and `-z`: 200 000 inputs against the Rust per float
type, and the Rust's `sqrt_and_cis_are_close`.

## Differences

- Generics become macros; Rust's tuple returns (`cis`) become out parameters.
