# @ajax/mesh-rt-wasm

The guest runtime of wasm CPU mesh programs, ported from alpha-rust-mesh's `crates/mesh-rt-wasm`: mesh-rt's
surface (`mesh_ctx`, `mesh_view`, `mesh_exchange`, `MESH_PROGRAM(init, task)`) over the `mesh::*`
imports of meshd's wasm executor. wasm32 only (`"targets": ["wasm32"]`).

A program includes `mesh/rt_wasm.h`, writes `init` and `task`, and `MESH_PROGRAM(init, task)` exports
`mesh_init`, `mesh_task` and `mesh_rt_abi`. A node is one CPU thread: `lane()` is 0 and `leader()` true.
Views are windows reached through bounds-checked imports (use the bulk `read`/`write` in hot loops);
linear memory is scratch. f32 in the cluster float mode is `@ajax/mesh-ftz`, whose operations on wasm32
are the exported `mesh_ftz_*` helpers meshd's float guard allows.

It also gives freestanding wasm32 code `memcpy`, `memmove`, `memset` and compiler-rt's `__multi3`.

## Differences

C has no OnceCell or slices: `mesh_ctx_get()` builds the context on first use, views are passed by value,
and Option results are `bool` plus an out parameter.

Tests: every guest in `programs/*-wasm`, through `@ajax/mesh-wasm-programs-check`.
