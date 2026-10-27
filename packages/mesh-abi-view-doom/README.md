# @ajax/mesh-abi-view-doom

programs/doom's view: a DDA raycaster's pose, map, columns and show region.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/view_doom.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/views.rs (`doom`): WIDTH, D_POS_X, D_POS_Y, D_DIR_X, D_DIR_Y, D_PLANE_X, D_PLANE_Y, D_ROWS, … (43 in all).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here. Its tables (string names, field lists) live in `src/`, so device code that uses them links the package for that target.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (79 checks), and 2 Rust const asserts that call functions
- `tests/golden.c`: its functions against 261 vectors computed by the Rust on the same inputs

## Differences

`MAP`'s rows are `char[17]` (16 cells and the NUL C adds to a string literal; GCC 15 warns about the Rust layout's unterminated `[u8; 16]`). `PLACE_STATE` and `PLACE_OUT` are arrays of `mesh_doom_region {off, len}`. `SNAP_BYTES` is written out (`D_SHOW + SH_DATA + 2 * OUT_BYTES`) because `show_bytes` is a function, not a constant expression, in C.

## Licence

MIT. See the repository's LICENSE.
