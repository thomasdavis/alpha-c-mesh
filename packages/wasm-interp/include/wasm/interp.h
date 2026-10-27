/* @ajax/wasm-interp: runs WebAssembly modules.
 *
 *   wasm_compile      bytes -> a compiled module (decoded, validated, lowered), shareable by threads
 *   wasm_instantiate  a compiled module + host functions + limits -> an instance (one thread's)
 *   wasm_call         calls a function of an instance; a trap is returned, never longjmp'd
 *
 * What the engine does, as meshd's wasmtime engine is configured (alpha-rust-mesh
 * mesh-host/src/wasm.rs `engine()`): every arithmetic NaN is canonical (@ajax/wasm-numeric);
 * no signal handlers (every memory and table access is bounds-checked); interrupts by epoch: an
 * instance given an epoch counter and a deadline traps with WASM_TRAP_INTERRUPT at the next loop
 * header or function entry once the counter reaches the deadline. Store limits as wasmtime's
 * StoreLimits: a memory may not grow past `memory_bytes` (memory.grow returns -1; a module whose
 * initial memory is larger fails to instantiate), at most `tables` tables and `memories`
 * memories. Proposals: the MVP, mutable globals, sign extension, saturating float-to-int,
 * multi-value, reference types, bulk memory, extended constant expressions (WASM_F_INTERP); not
 * SIMD, threads, multi-memory or memory64. */
#ifndef WASM_INTERP_H
#define WASM_INTERP_H

#include "wasm/module.h"
#include "wasm/numeric.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct wasm_compiled wasm_compiled;
typedef struct wasm_instance wasm_instance;

/* A host function: args in io[0..nparams), results to io[0..nresults) (i32/f32 in the low 32 bits).
 * Returns WASM_NUM_OK, or a trap code to trap the calling wasm. */
typedef int (*wasm_host_fn)(wasm_instance *inst, void *ctx, uint64_t *io);

typedef struct {
    const char *module, *name;
    const char *sig; /* "ii>i" over i l f d */
    wasm_host_fn fn;
    void *ctx;
} wasm_host_func;

typedef struct {
    uint64_t memory_bytes; /* 0: no limit beyond the module's own maximum */
    uint32_t tables;       /* 0: no limit */
    uint32_t memories;     /* 0: no limit */
    uint32_t stack_slots;  /* value stack, 64-bit slots; 0: 128 Ki */
    uint32_t call_depth;   /* frames; 0: 10 000 */
} wasm_store_limits;

/* Decodes, validates (under `features`, which must be within WASM_F_INTERP to run) and lowers.
 * The bytes are copied. NULL with err ("jit: ..." is left to the caller) on any failure. */
wasm_compiled *wasm_compile(const uint8_t *bytes, size_t len, wasm_features features, char *err, size_t errlen);
void wasm_compiled_free(wasm_compiled *c);
const wasm_module *wasm_compiled_module(const wasm_compiled *c);

/* Resolves imports by module, name and signature (wasmtime's messages: "unknown import: `m::n`
 * has not been defined", "incompatible import type for `m::n`"), applies limits, initialises
 * globals, tables, memory and segments, runs the start function. NULL with err on failure. */
wasm_instance *wasm_instantiate(const wasm_compiled *c, const wasm_host_func *imports, size_t nimports,
                                const wasm_store_limits *limits, void *user, char *err, size_t errlen);
void wasm_instance_free(wasm_instance *inst);
void *wasm_instance_user(const wasm_instance *inst);
const wasm_compiled *wasm_instance_compiled(const wasm_instance *inst);

/* The exported function `name`, checked against sig: its index, or -1 with err (wasmtime-like:
 * "failed to find function export `name`", "type mismatch with parameters"). */
int64_t wasm_instance_func(const wasm_instance *inst, const char *name, const char *sig, char *err, size_t errlen);

/* Calls function `func` (imports first): io holds its args and receives its results. WASM_NUM_OK
 * or the trap code (wasm/numeric.h). After a trap the instance must not be called again (its
 * state may be anywhere mid-call; meshd makes a fresh one). */
int wasm_call(wasm_instance *inst, uint32_t func, uint64_t *io);

/* The instance's memory 0: its bytes and size (both change when it grows); NULL without one. */
uint8_t *wasm_memory(const wasm_instance *inst, uint64_t *size);

/* Interrupts: trap at the next check once *epoch >= deadline. NULL epoch: never. */
void wasm_set_epoch(wasm_instance *inst, const _Atomic uint64_t *epoch);
void wasm_set_deadline(wasm_instance *inst, uint64_t deadline);
uint64_t wasm_deadline(const wasm_instance *inst);

#endif
