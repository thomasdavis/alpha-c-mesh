# @ajax/mesh-rt

The mesh device runtime. A program is C:

```c
#include "mesh/rt.h"

static void init(const mesh_ctx *ctx) { if (mesh_ctx_leader(ctx)) mesh_ctx_set_u32(ctx, 0, 1); }
static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) { return arg * 3 + 7; }

MESH_PROGRAM(init, task);
```

`MESH_PROGRAM` defines the kernel `mesh_main(mb, slot)` (the node loop, @ajax/mesh-rt-loop) and the global
`MESH_RT_ABI` meshd checks at load. Compiled for nvptx64 it is the GPU image (a sexc `"kind": "ptx"`
package, then `mesh-ptx-link` for the catalog); compiled for the host, `mesh_main` runs on
@ajax/mesh-rt-sim's threads in tests.

The runtime is split into atomic packages, all header-only and inlined into the kernel:

| Package | What |
|---|---|
| @ajax/mesh-rt-ptx | PTX primitives |
| @ajax/mesh-rt-sim | the same primitives on CPU threads, and the launcher |
| @ajax/mesh-rt-prim | picks one, plus f32 helpers |
| @ajax/mesh-rt-view | bounds-checked views, faults |
| @ajax/mesh-rt-block | block collectives in a fixed order, shared memory |
| @ajax/mesh-rt-ctx | `mesh_ctx` |
| @ajax/mesh-rt-loop | the node loop |
| @ajax/mesh-rt-linalg | exact-mode GEMM tile and GEMV |

- **Ported from:** alpha-rust-mesh `crates/mesh-rt/src/lib.rs` (`mesh_program!`).
- **Differences:** the `MESH_RT_ABI` symbol is made with an asm label, because `MESH_RT_ABI` is also the
  contract's constant (6) in C. Rust's panic handler traps; C has no panics (bounds checks fault instead).
