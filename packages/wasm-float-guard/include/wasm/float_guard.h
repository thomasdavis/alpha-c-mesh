/* @ajax/wasm-float-guard: meshd's load-time check of a wasm image (alpha-rust-mesh
 * mesh-host/src/wasm.rs `float_check`).
 *
 * An image declaring the cluster float mode (`float = "ftz"`, the default, required for lockstep)
 * may hold f32 arithmetic, compares and f32<->f64 converts only inside its exported `mesh_ftz_*`
 * helpers (mesh-ref ftz.rs / @ajax/mesh-ftz: flush in, op, flush out), and no SIMD at all; no image
 * may use threads, shared memory or relaxed SIMD, or import anything but `mesh` functions.
 * Errors start with "jit: " (not valid wasm), "float: " or "load: ", as meshd's load reasons. */
#ifndef WASM_FLOAT_GUARD_H
#define WASM_FLOAT_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What the check saw: functions, the exported FTZ helpers, f32 ops (all in helpers when ftz). */
typedef struct {
    uint32_t funcs;
    uint32_t helpers;
    uint32_t f32_ops;
} wasm_float_report;

/* The ops the cluster float mode flushes: f32 arithmetic, compares, f32 <-> f64 converts. Loads,
 * stores, constants, reinterprets, selects and int <-> f32 converts move or make values without a
 * flush on the GPU too. */
bool wasm_flushing_f32(uint32_t op);

bool wasm_float_check(const uint8_t *wasm, size_t len, bool ftz, wasm_float_report *rep, char *err, size_t errlen);

#endif
