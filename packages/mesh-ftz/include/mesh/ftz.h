/* @ajax/mesh-ftz: the cluster float mode `Z` (mesh-ref ftz.rs).
 *
 * A Z value is a plain float; what makes it Z is that every operation goes through these functions,
 * each the PTX op it stands for: add/sub/mul/div .rn.ftz, abs/neg .ftz, setp .ftz, cvt .ftz. A
 * subnormal input is flushed to the zero of its sign, the IEEE result is correctly rounded, and a
 * subnormal result is flushed the same way. Loads, stores and moves never flush (a float variable
 * keeps its bits); only operations do.
 *
 * On wasm32 every float operation of Z is an exported, never-inlined function `mesh_ftz_*`
 * (defined in ftz.c), and nothing else here uses a float instruction: flushes and NaN tests are
 * integer bit tests. meshd's load-time float guard refuses an FTZ wasm image with a float
 * instruction anywhere else. Elsewhere they are static inline.
 *
 * Freestanding: only <stdint.h>, <stdbool.h>, <float.h> and <stdatomic.h>. Code using Z must be
 * compiled with -ffp-contract=off (no fused multiply-add) on a target with FLT_EVAL_METHOD 0. */
#ifndef MESH_FTZ_H
#define MESH_FTZ_H

#include <float.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

_Static_assert(FLT_EVAL_METHOD == 0, "Z needs float arithmetic in float precision (no x87)");

/* Subnormals flushed by Z operations since the process started (inputs and results). */
extern _Atomic uint64_t mesh_ftz_flush_count;
uint64_t mesh_ftz_flushes(void);

typedef union {
    float f;
    uint32_t u;
} mesh_ftz_pun;

static inline uint32_t mesh_ftz_bits(float x) {
    mesh_ftz_pun p;
    p.f = x;
    return p.u;
}

static inline float mesh_ftz_from_bits(uint32_t b) {
    mesh_ftz_pun p;
    p.u = b;
    return p.f;
}

/* A subnormal to the zero of its sign; anything else unchanged. A bit test, no float instruction. */
static inline float mesh_ftz_flush(float x) {
    uint32_t b = mesh_ftz_bits(x);
    if ((b & 0x7f800000u) == 0 && (b & 0x007fffffu) != 0) {
        atomic_fetch_add_explicit(&mesh_ftz_flush_count, 1, memory_order_relaxed);
        return mesh_ftz_from_bits(b & 0x80000000u);
    }
    return x;
}

/* setp.nan.ftz.f32 against itself: a bit test. */
static inline bool mesh_ftz_is_nan(float x) { return (mesh_ftz_bits(x) & 0x7fffffffu) > 0x7f800000u; }

/* cvt.rn.f32.u32 and cvt.rn.f32.s32: an integer is never subnormal, so nothing to flush. */
static inline float mesh_ftz_from_u32(uint32_t x) { return (float)x; }
static inline float mesh_ftz_from_i32(int32_t x) { return (float)x; }

/* A NaN operand's result: x86's rule with a as the first operand (the NaN of a, else of b, quieted).
 * C, like Rust, leaves the operand order of a commutative + or * to the compiler, so without this the
 * payload of NaN + NaN would depend on how the code happened to compile. */
static inline float mesh_ftz_nan2(float a, float b) {
    uint32_t x = mesh_ftz_bits(a);
    if ((x & 0x7fffffffu) <= 0x7f800000u) x = mesh_ftz_bits(b);
    return mesh_ftz_from_bits(x | 0x00400000u);
}

#define MESH_FTZ_BINOP(a, b, op)                                                                   \
    do {                                                                                           \
        a = mesh_ftz_flush(a);                                                                     \
        b = mesh_ftz_flush(b);                                                                     \
        if (mesh_ftz_is_nan(a) || mesh_ftz_is_nan(b)) return mesh_ftz_nan2(a, b);                  \
        return mesh_ftz_flush(a op b);                                                             \
    } while (0)

#define MESH_FTZ_LESS (-1)
#define MESH_FTZ_EQUAL 0
#define MESH_FTZ_GREATER 1
#define MESH_FTZ_UNORDERED 2

#if defined(__wasm32__)
/* The only places an FTZ wasm image may hold float instructions (ftz.c). */
float mesh_ftz_add(float a, float b);
float mesh_ftz_sub(float a, float b);
float mesh_ftz_mul(float a, float b);
float mesh_ftz_div(float a, float b);
float mesh_ftz_neg(float a);
float mesh_ftz_abs(float a);
double mesh_ftz_to_f64(float a);
float mesh_ftz_from_f64(double a);
int32_t mesh_ftz_cmp(float a, float b);
#else
static inline float mesh_ftz_add(float a, float b) { MESH_FTZ_BINOP(a, b, +); }
static inline float mesh_ftz_sub(float a, float b) { MESH_FTZ_BINOP(a, b, -); }
static inline float mesh_ftz_mul(float a, float b) { MESH_FTZ_BINOP(a, b, *); }
static inline float mesh_ftz_div(float a, float b) { MESH_FTZ_BINOP(a, b, /); }
/* neg.ftz: the sign bit of the flushed input flipped (a NaN keeps its payload). */
static inline float mesh_ftz_neg(float a) { return mesh_ftz_from_bits(mesh_ftz_bits(mesh_ftz_flush(a)) ^ 0x80000000u); }
/* abs.ftz: the sign bit of the flushed input cleared. */
static inline float mesh_ftz_abs(float a) { return mesh_ftz_from_bits(mesh_ftz_bits(mesh_ftz_flush(a)) & 0x7fffffffu); }
/* cvt.ftz.f64.f32 and cvt.rn.ftz.f32.f64. */
static inline double mesh_ftz_to_f64(float a) { return (double)mesh_ftz_flush(a); }
static inline float mesh_ftz_from_f64(double a) { return mesh_ftz_flush((float)a); }
/* setp.ftz: MESH_FTZ_LESS, _EQUAL, _GREATER, or _UNORDERED (a NaN), of the flushed inputs. */
static inline int32_t mesh_ftz_cmp(float a, float b) {
    a = mesh_ftz_flush(a);
    b = mesh_ftz_flush(b);
    if (a < b) return MESH_FTZ_LESS;
    if (a > b) return MESH_FTZ_GREATER;
    if (a == b) return MESH_FTZ_EQUAL;
    return MESH_FTZ_UNORDERED;
}
#endif

/* The compares of setp.{eq,ne,lt,le,gt,ge}.ftz.f32: false whenever a NaN is involved, except ne. */
static inline bool mesh_ftz_eq(float a, float b) { return mesh_ftz_cmp(a, b) == MESH_FTZ_EQUAL; }
static inline bool mesh_ftz_ne(float a, float b) { return mesh_ftz_cmp(a, b) != MESH_FTZ_EQUAL; }
static inline bool mesh_ftz_lt(float a, float b) { return mesh_ftz_cmp(a, b) == MESH_FTZ_LESS; }
static inline bool mesh_ftz_gt(float a, float b) { return mesh_ftz_cmp(a, b) == MESH_FTZ_GREATER; }
static inline bool mesh_ftz_le(float a, float b) {
    int32_t c = mesh_ftz_cmp(a, b);
    return c == MESH_FTZ_LESS || c == MESH_FTZ_EQUAL;
}
static inline bool mesh_ftz_ge(float a, float b) {
    int32_t c = mesh_ftz_cmp(a, b);
    return c == MESH_FTZ_GREATER || c == MESH_FTZ_EQUAL;
}

#endif
