# @ajax/mesh-abi

The whole mesh contract in one include, for code ported from Rust's `use mesh_abi::v2::*`:

```c
#include "mesh/abi.h"
```

It includes every `@ajax/mesh-abi-*` package and holds the Rust const asserts that span more than one of
them (`_Static_assert`s in the header; the ones that call functions run in `tests/layout.c`). New code should
depend on the atomic packages it actually uses instead.

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): `crates/mesh-abi`
(`lib.rs`, `v2.rs`, `v2/pipe.rs`, `v2/views.rs`, `v2/wasm.rs`). docs/port/abi.md maps every Rust item to its
C package.

## Tests

- `tests/layout.c`: the cross-package Rust const asserts that call functions (55), the FRAMES guard (ids
  dense 1..=32), and the Rust's `layout_tests::regions_have_no_aliased_fields` over all 13 field tables.
- @ajax/mesh-abi-ptx-check and @ajax/mesh-abi-wasm-check build the whole contract for nvptx64 and wasm32.

## Device code

Freestanding: compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`.

## Licence

MIT. See the repository's LICENSE.
