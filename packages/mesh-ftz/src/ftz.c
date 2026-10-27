/* The flush counter and, on wasm32, the exported float operations of Z (mesh-ref ftz.rs, `ops`). */
#include "mesh/ftz.h"

_Atomic uint64_t mesh_ftz_flush_count = 0;

uint64_t mesh_ftz_flushes(void) { return atomic_load_explicit(&mesh_ftz_flush_count, memory_order_relaxed); }

#if defined(__wasm32__)
#define MESH_FTZ_EXPORT(name) __attribute__((export_name(#name), noinline))

MESH_FTZ_EXPORT(mesh_ftz_add) float mesh_ftz_add(float a, float b) { MESH_FTZ_BINOP(a, b, +); }
MESH_FTZ_EXPORT(mesh_ftz_sub) float mesh_ftz_sub(float a, float b) { MESH_FTZ_BINOP(a, b, -); }
MESH_FTZ_EXPORT(mesh_ftz_mul) float mesh_ftz_mul(float a, float b) { MESH_FTZ_BINOP(a, b, *); }
MESH_FTZ_EXPORT(mesh_ftz_div) float mesh_ftz_div(float a, float b) { MESH_FTZ_BINOP(a, b, /); }
/* As Rust's `-x` and `x.abs()` compile on wasm32: f32.neg and f32.abs, bit operations. */
MESH_FTZ_EXPORT(mesh_ftz_neg) float mesh_ftz_neg(float a) { return -mesh_ftz_flush(a); }
MESH_FTZ_EXPORT(mesh_ftz_abs) float mesh_ftz_abs(float a) { return __builtin_fabsf(mesh_ftz_flush(a)); }
MESH_FTZ_EXPORT(mesh_ftz_to_f64) double mesh_ftz_to_f64(float a) { return (double)mesh_ftz_flush(a); }
MESH_FTZ_EXPORT(mesh_ftz_from_f64) float mesh_ftz_from_f64(double a) { return mesh_ftz_flush((float)a); }
MESH_FTZ_EXPORT(mesh_ftz_cmp) int32_t mesh_ftz_cmp(float a, float b) {
    a = mesh_ftz_flush(a);
    b = mesh_ftz_flush(b);
    if (a < b) return MESH_FTZ_LESS;
    if (a > b) return MESH_FTZ_GREATER;
    if (a == b) return MESH_FTZ_EQUAL;
    return MESH_FTZ_UNORDERED;
}
#endif
