# @ajax/mesh-abi-host-status

The host status block's first region: uptime, peers, rates, names, isolation, cluster size.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/host_status.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2.rs: S_UPTIME_MS, S_EPOCH, S_SYSTEM, S_PEER_STATE, S_PROGRAMS, S_SMS_TOTAL, S_GREEN, S_TASKS_PER_S, … (32 in all).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (32 checks)

## Licence

MIT. See the repository's LICENSE.
