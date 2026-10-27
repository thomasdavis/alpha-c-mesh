# @ajax/mesh-abi-pipe-ring

A pipe inbox's two lock-free MPMC rings: laps, record sums, reserve and commit.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/pipe_ring.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/pipe.rs (the ring: PI_*, Ring, Inbox, owner tags, shapes).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (11 checks)
- `tests/golden.c`: its functions against 267 vectors computed by the Rust on the same inputs
- `tests/ring.c`: the Rust's ring tests, one for one (shape_and_laps, fifo_full_and_wrap, mpmc_threads_lose_and_duplicate_nothing with C11 threads, position_is_in_the_sum, reserve_push_is_bounded, skip_dead_waits_for_the_front), over C11 atomics (`tests/mem.h`)

## Differences

`Option<u32>` results are a bool plus an out parameter; `skip_dead`'s `Option<bool>` is 1 / 0 / -1. `abandoned` takes a callback and a context instead of a closure. Methods are functions taking the ring or inbox first.

## Licence

MIT. See the repository's LICENSE.
