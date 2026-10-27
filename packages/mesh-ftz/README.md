# @ajax/mesh-ftz

The cluster float mode `Z`: f32 arithmetic exactly as the mesh's PTX does it (`add/sub/mul/div .rn.ftz`,
`abs/neg .ftz`, `setp .ftz`, `cvt .ftz`). A subnormal input is flushed to the zero of its sign, the IEEE
result is correctly rounded, and a subnormal result is flushed the same way. Every CPU reference of the
mesh is written in these operations, so a CPU computes the GPU's bits.

```c
#include "mesh/ftz.h"
float y = mesh_ftz_add(mesh_ftz_mul(a, x), b);   /* never a * x + b */
if (mesh_ftz_lt(y, 0.0f)) ...
```

- A `Z` value is a plain `float`: loads, stores and moves keep the bits; only `mesh_ftz_*` operations flush.
- `mesh_ftz_flushes()` counts the subnormals flushed since the process started (inputs and results).
- On wasm32 the float operations are exported, never-inlined functions (`mesh_ftz_add`, …) and nothing
  else uses a float instruction, so meshd's load-time float guard can prove an FTZ image never bypasses
  the flush. Elsewhere they are `static inline`.
- Freestanding (no libc). Compile code that uses it with `-ffp-contract=off` on a target whose
  `FLT_EVAL_METHOD` is 0 (x86-64, arm64, wasm32, nvptx64; a static assert checks).

**Ported from** `crates/mesh-ref/src/ftz.rs` (alpha-rust-mesh `00b526b`): `flush`, the `ops` module, `Z`.

**Tested** by the two Rust unit tests, ported, and by golden digests from the Rust: all 44×44 pairs of
special values (zeros, subnormals, the normal boundary, max, infinities, quiet and signalling NaNs with
payloads), four sweeps of a million random pairs (any bits; tiny exponents; products and quotients
crossing the subnormal boundary) through every operation, and a million f64 → f32 converts. Each digest
covers every result's bits, and the flush counts must match too. Passes with gcc and clang, -O0 and -O2.

## Differences

- **NaN payloads when both operands are NaN.** Rust leaves the operand order of a commutative `+` or `*`
  to the compiler, and x86 returns the NaN of the instruction's first operand, so the Rust result's
  payload depends on how the code compiled. Here it is always `a`'s NaN (else `b`'s), quieted — what the
  Rust build produced in every case measured. (A GPU returns the canonical NaN either way.)
