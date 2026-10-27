# @ajax/mesh-abi-header

The mailbox header (H_*): 0x0000..0x0100.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/header.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2.rs: H_MAGIC, H_VERSION, H_PEER, H_BLINK, H_EPOCH, H_PROG_SEQ, H_SMS, H_SYSTEMS, … (21 in all).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here. Its tables (string names, field lists) live in `src/`, so device code that uses them links the package for that target.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (59 checks)

## Licence

MIT. See the repository's LICENSE.
