# Port: the device runtime and the programs

alpha-rust-mesh's `crates/mesh-rt` (the device runtime), the GPU and native programs under `programs/`,
and `scripts/build-programs.sh`, `ptx-link.sh` and `ptx-float-check.sh`, in C.

## How it is checked

There is no GPU on the machine this was ported on, so every program is checked twice over:

- **Its PTX builds and obeys the cluster float mode.** `sexc build` compiles each `ptx` package for
  nvptx64 with clang; `mesh-ptx-link` links it with `--denormal-fp-math-f32=preserve-sign` (so every f32
  instruction is `.ftz`) and exports only `mesh_main` and `MESH_RT_ABI`; the result is checked by the
  ported float rules **and** by the original awk script (`ptx-float-check.sh`, run by hand on every
  program: both agree), for `.target sm_86`, the entry and the global. `mesh-build-programs` built the
  whole catalog: 12 GPU images, 3 native programs, 7 variants.
- **The same C runs on the CPU against the references, bit for bit.** `@ajax/mesh-rt-sim` implements the
  device primitives with POSIX threads (a node is `warps` x 32 threads; warp collectives are exchanges
  between barriers; C11 atomics with the same orders) and runs every simulated thread in f32
  flush-to-zero (MXCSR FTZ|DAZ), the PTX `.ftz` semantics. Each program's tests run its real `mesh_main`
  through the real node loop on a host mailbox (@ajax/mesh-rt-sim-mailbox: tickets, marks, request
  window, input queue, inboxes) and compare with REF's C references (`mesh-ref-*`, `pathtrace-*`,
  themselves golden-tested against the Rust). Subnormal-heavy inputs (blocks, pathtrace) match exactly,
  so the simulator's FTZ is the cluster float mode's.
- **kcheck's checks** for programs are ported into the programs' tests where they apply on one machine:
  B1-B3 (blocks), 3, 3b and the follow part of 3c (doom), 4 (http), the train/infer/hello/tick checks,
  GM* (matmul), GX* per op (gpt2x), PT* (pathtrace), and the sha checks.

What only a real GPU can verify: that ptxas accepts the PTX (the output follows what LLVM's NVPTX
backend emits for the same constructs as rustc's, but was never JIT-compiled here); the timing behaviour
(poll periods, heartbeats, load records, `POSE_WAIT_NS`, the lockstep budget) and performance; the
exact memory-ordering behaviour of `.sys` scope against a host CPU; and multi-GPU lockstep through a real
peer relay (the train test mirrors the exchange pages with a thread instead).

## Rust → C

| Rust | C package | C |
|---|---|---|
| mesh-rt `ptx.rs` | @ajax/mesh-rt-ptx (device), @ajax/mesh-rt-sim (CPU), @ajax/mesh-rt-prim (picks; f32 helpers) | `mesh_rt_tid`, `mesh_rt_ld_acquire`, `mesh_rt_shfl_idx`, `mesh_rt_bar_sync`, ... `mesh_rt_warp_sum` |
| `ctx.rs` `View`, `fault` | @ajax/mesh-rt-view | `mesh_view`, `mesh_view_u32` ..., `mesh_view_sub`, `mesh_rt_fault` |
| `block.rs` | @ajax/mesh-rt-block | `mesh_rt_block_{reduce,scan,broadcast}_{u32,f32,u64}`, `mesh_rt_warp_{reduce,scan}_*`, `mesh_shared`, `mesh_rt_max_f32` |
| `ctx.rs` `Ctx`, `Exchange`, `Gw` | @ajax/mesh-rt-ctx | `mesh_ctx`, `mesh_ctx_*`, `mesh_exchange`, `mesh_rt_gw` |
| `ring.rs` `run` | @ajax/mesh-rt-loop | `mesh_rt_run` |
| `lib.rs` `mesh_program!` | @ajax/mesh-rt | `MESH_PROGRAM(init, task);` |
| `linalg.rs` | @ajax/mesh-rt-linalg | `mesh_linalg_gemm_tile`, `mesh_linalg_gemv_cols` |
| (none: testing without a GPU) | @ajax/mesh-rt-sim, @ajax/mesh-rt-sim-mailbox | `mesh_sim_start`, `mesh_sim_install`, `mesh_sim_call`, ... |
| `programs/<name>/src` (GPU) | `programs/<name>` = @ajax/mesh-prog-<name> (`kind: ptx`) | hello, tick, infer, train, blocks, doom, pipehash, matmul, http, sha256d, gpt2x, pathtrace |
| `programs/<name>/src/main.rs` (native) | `programs/<name>` (`kind: executable`, on exec's @ajax/mesh-worker) | pipesink, pipesrc-n, gemm |
| `programs/*/program.toml`, `variants/` | kept as they are, beside each package | |
| `scripts/build-programs.sh` | apps/mesh-build-programs, @ajax/mesh-catalog-toml, @ajax/mesh-source-hash, @ajax/sha256-stream | |
| `scripts/ptx-link.sh` | apps/mesh-ptx-link | |
| `scripts/ptx-float-check.sh` | apps/mesh-ptx-float-check, @ajax/mesh-ptx-float-rules | |

## Writing a GPU program in C

```c
#include "mesh/rt.h"
static void init(const mesh_ctx *c) { if (mesh_ctx_leader(c)) mesh_ctx_set_u32(c, 0, 1); }
static uint32_t task(const mesh_ctx *c, uint32_t op, uint32_t arg) { return arg * 3 + 7; }
MESH_PROGRAM(init, task);
```

A `sexc.json` with `"kind": "ptx"`, `"cflags": ["-ffp-contract=off"]` (no fused multiply-adds) and a
dependency on @ajax/mesh-rt. Rust generics and closures become typed functions with function-pointer
operations and callbacks with a `void *user`; every runtime function is `static inline
__attribute__((always_inline))`, so the pointers are constants and the PTX has one entry and no calls
(train keeps its `noinline` `sin_pi`, pathtrace a few, as in the Rust).

## Not done

- **`programs/gpt2`** (native): it is built on candle (a Rust ML framework with its own CUDA kernels)
  for its fast mode and HF tokenizers, and loads gpt2x in-process through mesh-host. A C port needs a
  GPT-2 BPE tokenizer, daemon's `mesh-gpt2x-host`, and a replacement for candle's fast path.
- **`programs/ane`** and every `program.metal`: Apple only, not C.
- **The wasm images** (`programs/<name>/wasm`) are the wasm builder's (`programs/<name>-wasm`);
  `mesh-build-programs` picks them up as `kind: wasm` packages.

## For the other areas

- **exec:** `packages/mesh-worker-ctx/src/ctx.c` `mesh_ctx_pipe_len` tests `mesh_pipe_target(...) <= 0`,
  but it returns 0 on success: it must be `< 0`. Until then a native program can never see its own
  inbox's length (pipesink's test detects it and skips its drain checks; with the fix they pass).
  `mesh-cpuuse`'s test (`cpu >= 20000000`) fails on a loaded machine, and `mesh-native`'s lifecycle test
  leaks 1351 bytes under ASan.
- **sexc:** test-only dependencies would let matmul's and pathtrace's tests live in their programs'
  packages; today `@ajax/mesh-prog-matmul-check` and `@ajax/mesh-prog-pathtrace-check` (private) hold
  them, because a `ptx` package can only depend on libraries that build for nvptx64.
- **Name collisions:** exec's @ajax/mesh-worker-ctx and this area's @ajax/mesh-rt-view/-ctx both define
  `mesh_view` and `mesh_ctx_*` (on purpose: native and GPU programs read alike). A file must include one
  or the other, never both.
