# @ajax/mesh-abi-view-pipedemo

The pipe demo's records and views: msg, digest, sources, pipehash, sinks.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/view_pipedemo.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/views.rs (`pipedemo`): MSG_TYPE, MSG_BYTES, DIGEST_TYPE, DIGEST_BYTES, R_SEQ, R_STAMP_LO, R_STAMP_HI, R_ORIGIN, … (53 in all).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (53 checks)
- `tests/golden.c`: its functions against 64 vectors computed by the Rust on the same inputs

## Differences

`quantile` is `hist_quantile` in @ajax/hist-quantile and `compress` is `sha256_compress` in @ajax/sha256-compress. Functions write into caller arrays instead of returning them.

## Licence

MIT. See the repository's LICENSE.
