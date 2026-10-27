# @ajax/mesh-abi-pipe

Pipes between programs: push into a connected inbox, pop your own.

Part of the mesh contract: the memory, control, snapshot and peer protocols shared by meshd, its GPU
programs (CUDA kernels, wasm lanes, native workers) and the tools. Every side compiles the same
definitions, so an offset can never disagree.

```c
#include "mesh/abi/pipe.h"
```

## Ported from

[alpha-rust-mesh](https://github.com/rsafier/alpha-rust-mesh) (Richard Safier, MIT): crates/mesh-abi/src/v2/pipe.rs (PipeErr, codes, read_block, target, flight_begin/end, push, pop).

Names: Rust's `NAME` is `MESH_NAME`; a constant of a Rust module `m` is `MESH_M_NAME` (unless it already starts with `M_`); a function `f` is `mesh_f` (`mesh_m_f`). Offsets that are `usize` in the Rust are plain integer constants or `size_t` results, `u32` constants carry a `u` suffix, `u64` use `UINT64_C`.

## Device code

The header is freestanding (`<stdint.h>`, `<stddef.h>`, `<stdbool.h>` only): it compiles for the host, `nvptx64-nvidia-cuda` and `wasm32`. Functions the Rust marks `const fn` are `static inline` here.

## Tests

- `tests/constants.c`: every constant against the value the Rust prints (5 checks)
- `tests/pipe.c`: the Rust's table-level pipe tests, one for one (host_push_pop_through_the_tables, hold_and_flight), plus every result's code, over an 8 MiB mailbox of C11 atomics

## Differences

Rust's `Result<bool, PipeErr>` is an int: 1 = Ok(true), 0 = Ok(false), -e = Err(e), with `mesh_pipe_err` numbered so that -NOT_CONNECTED, -SIZE and -GONE are the runtimes' PIPE_* codes. `Option` results are a bool plus an out parameter. Records are (pointer, length) instead of slices.

## Licence

MIT. See the repository's LICENSE.
