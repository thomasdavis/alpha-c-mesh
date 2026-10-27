# @ajax/mesh-abi-node

Node records (N_*): a node's SM, heartbeat, tasks and busy time.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/node.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2.rs: PROG_NODES, PROG_MAX_NODES, NODE_STRIDE, PROG_NODES_BYTES, N_SM, N_HEARTBEAT, N_TASKS, N_LAST_OP, … (10 in all).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here. Its tables (string names, field lists) live in `src/`, so device code that uses them links the package for that target.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (20 checks)
- `tests/golden.c`: its functions against 18 vectors computed by the Rust on the same inputs

## Licence

MIT. See the repository's LICENSE.
