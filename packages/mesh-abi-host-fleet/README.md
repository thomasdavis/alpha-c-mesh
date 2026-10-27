# @ajax/mesh-abi-host-fleet

The fleet region: leader, term, versions, members' reconcile status, the lockstep group.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/host_fleet.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2.rs: S_FLEET, S_LEADER, S_ROLE, S_TERM, S_STATE_VER, S_APPLIED_VER, S_UP_MASK, S_SUSPECT_MASK, … (41 in all).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here. Its tables (string names, field lists) live in `src/`, so device code that uses them links the package for that target.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (59 checks)
- `tests/golden.c`: its functions against 5 vectors computed by the Rust on the same inputs

## Licence

MIT. See the repository's LICENSE.
