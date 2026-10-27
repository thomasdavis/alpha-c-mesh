# @ajax/mesh-abi-place-ops

F_PLACE ops: pause, paused, set, done, fail.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/place_ops.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2.rs: PL_REQ, PL_RESULT, PL_PAUSE, PL_PAUSED, PL_SET, PL_DONE, PL_FAIL.

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (7 checks)

## Licence

MIT. See the repository's LICENSE.
