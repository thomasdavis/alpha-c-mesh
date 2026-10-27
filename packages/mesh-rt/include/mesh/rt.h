/* @ajax/mesh-rt: the device runtime every mesh program is built on (contract v2). A program writes
 *
 *     #include "mesh/rt.h"
 *     static void init(const mesh_ctx *ctx) { .. }                          // op 1 (OP_INIT)
 *     static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) // op 0 and 2..
 *     { .. }
 *     MESH_PROGRAM(init, task);
 *
 * which expands to the kernel `mesh_main(mb, slot)` -- the node loop over the
 * slot's control block and ring (@ajax/mesh-rt-loop) -- and the global
 * `MESH_RT_ABI` meshd checks at load. Every task runs on one node, a block of
 * 32 x `warps` threads (the manifest's `warps`, default 1): every thread calls
 * init/task; the task's result is thread 0's return value. The node has the
 * runtime's shared memory and the program's `shared_bytes`
 * (mesh_ctx_shared) and the block collectives (@ajax/mesh-rt-block). Every
 * runtime function is inlined into the kernel, so one PTX per program.
 *
 * Compiled for nvptx64 (clang --target=nvptx64-nvidia-cuda) it is the GPU
 * program; compiled for the host, the same source runs on @ajax/mesh-rt-sim's
 * threads (mesh_sim_start(mesh_main, ...)) for tests.
 * Ported from alpha-rust-mesh crates/mesh-rt/src/lib.rs. */
#ifndef MESH_RT_H
#define MESH_RT_H

#include <stdbool.h>
#include <stdint.h>

#include "mesh/abi.h"
#include "mesh/rt/block.h"
#include "mesh/rt/ctx.h"
#include "mesh/rt/loop.h"
#include "mesh/rt/prim.h"
#include "mesh/rt/view.h"

/* The program's kernel: `nodes` blocks of 32 x `warps` threads (PT_NODES, PT_WARPS). */
#if defined(__NVPTX__)
#define MESH_PROGRAM(INIT, TASK)                                                                                    \
    __attribute__((nvptx_kernel)) void mesh_main(uint8_t *mb, uint32_t slot) { mesh_rt_run(mb, slot, INIT, TASK); } \
    /* The runtime contract this image was built against (meshd refuses a mismatch). */                           \
    __attribute__((used)) const uint32_t mesh_rt_abi_symbol_ __asm__("MESH_RT_ABI") = MESH_RT_ABI
#else
void mesh_main(uint8_t *mb, uint32_t slot);
#define MESH_PROGRAM(INIT, TASK)                                                       \
    void mesh_main(uint8_t *mb, uint32_t slot) { mesh_rt_run(mb, slot, INIT, TASK); } \
    const uint32_t mesh_rt_abi_symbol_ = MESH_RT_ABI
#endif

#endif
