/* @ajax/mesh-exec-wasm: the wasm executor (mesh-abi BACKEND_WASM), ported from alpha-rust-mesh
 * mesh-host/src/wasm.rs: CPU programs as WebAssembly modules run inside meshd, beside the GPU
 * backend, on every platform.
 *
 * - Images. `<name>.wasm` beside program.toml. The compile -- here @ajax/wasm-interp's decode,
 *   validate and lower; in the Rust, Cranelift -- runs on the driver's JIT thread; with --wasm-cache
 *   DIR the module is kept there (@ajax/mesh-exec-wasm-cache). The image hash is the PTX's when the
 *   catalog has one, else the wasm's, with `param` folded in.
 * - One engine: NaN canonicalisation, no signal handlers (explicit bounds checks), epoch
 *   interruption, no threads.
 * - The float guard (@ajax/wasm-float-guard), at load.
 * - Lanes. A running slot is `nodes` lease lanes (@ajax/mesh-lease): one thread per node, each with
 *   its own instance, taking the slot's tickets one at a time and running mesh_init /
 *   mesh_task(op, arg). Linear memory is scratch; program state lives in the view, reached through
 *   the mesh::* imports (@ajax/mesh-exec-wasm-imports).
 * - Isolation. Each lane's memory is capped at the manifest's mem_mb; each task has an epoch
 *   deadline of task_ms (an epoch thread ticks every ms while lanes run; the wait in a lockstep
 *   exchange re-arms it): an overrun traps (FAULT_TIME), any other trap is FAULT_TRAP, and the lane
 *   re-instantiates while the driver fails the slot -- meshd and every other slot run on.
 * - Lifecycle. A wasm slot is isolated and not resident: its load, upgrade, rollback and unload
 *   halt only itself, and a GPU whole-context blink never halts it (its lanes ignore H_BLINK). */
#ifndef MESH_EXEC_WASM_H
#define MESH_EXEC_WASM_H

#include "mesh/backend.h"
#include "mesh/exec_wasm_imports.h"
#include "wasm/float_guard.h"
#include "wasm/interp.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's tick while lanes run (a task's budget is task_ms ticks). */
#define MESH_WASM_TICK_NS 1000000ull

typedef struct mesh_wasm_rt mesh_wasm_rt;

/* What meshd shares with every lane and compile: the cache dir and the epoch thread. NULL with
 * *err ("--wasm-cache DIR: ..."). */
mesh_wasm_rt *mesh_wasm_rt_new(const char *cache, char **err);
void mesh_wasm_rt_free(mesh_wasm_rt *rt);
/* The engine's identity for cached modules. */
uint64_t mesh_wasm_engine_hash(void);
/* The module for wasm (from the cache when it holds it): *how is "compiled" or "cached". */
wasm_compiled *mesh_wasm_rt_module(mesh_wasm_rt *rt, const char *name, const uint8_t *wasm, size_t n, double *ms,
                                   const char **how, char **err);

/* A compiled wasm image (the Kernel of a wasm slot). */
typedef struct {
    char *name;
    wasm_compiled *module;
    mesh_wasm_rt *rt;
    uint32_t rt_abi;
    uint64_t wasm_hash; /* fnv1a64 of the .wasm (the image hash is the PTX's when the catalog has one) */
    uint64_t mem_bytes;
    uint32_t task_ms;
    wasm_float_report float_report;
    _Atomic int refs; /* the kernel and each running lane (the Rust's InstancePre clones) */
} mesh_wasm_program;

/* A fresh instance of p for (slot, node of nodes) on mailbox base, its lane data in *data (which
 * must outlive the instance). NULL with *err ("load: <name>: instantiate: ..."). */
wasm_instance *mesh_wasm_instantiate(const mesh_wasm_program *p, mesh_mailbox mb, uint32_t slot, uint32_t node,
                                     uint32_t nodes, mesh_wasm_lane_data *data, char **err);

/* The executor: mailbox is the shared mailbox (the GPU backend's when beside it; its own for a
 * CPU-only meshd, `--backend wasm`, and kcheck); cache: --wasm-cache or NULL. */
bool mesh_wasm_backend_open(uint8_t *mailbox, const char *cache, mesh_backend **out, char **err);
/* The program behind a wasm kernel (tests, tools). */
const mesh_wasm_program *mesh_wasm_kernel_program(const mesh_kernel *k);

/* The P_FAULT word for a failed call: an interrupt is the epoch deadline (FAULT_TIME); any other
 * trap FAULT_TRAP | (wasmtime's trap code + 1) << 8. */
uint32_t mesh_wasm_trap_fault(int trap);

#endif
