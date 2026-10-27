/* @ajax/wasm-numeric: WebAssembly's numeric instructions (spec 4.3), deterministic.
 *
 * Floats follow IEEE 754 in the default rounding mode, with no fused multiply-add (build with
 * -ffp-contract=off, on a target with FLT_EVAL_METHOD 0) and, as wasmtime's engine in meshd
 * (`cranelift_nan_canonicalization(true)`), every NaN an arithmetic operation produces is the
 * canonical one: add, sub, mul, div, sqrt, min, max, ceil, floor, trunc, nearest, promote, demote.
 * neg, abs and copysign are bit operations and keep a NaN's payload, as do loads, stores, selects
 * and reinterprets.
 *
 * Operations that can trap return a trap code (WASM_NUM_OK when they didn't) and write the result
 * through a pointer. Header-only. */
#ifndef WASM_NUMERIC_H
#define WASM_NUMERIC_H

#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

_Static_assert(FLT_EVAL_METHOD == 0, "wasm floats need float and double evaluated in their own precision");

/* Trap codes, numbered as wasmtime's `Trap` (what meshd's FAULT_TRAP word carries). */
enum {
    WASM_NUM_OK = -1,
    WASM_TRAP_STACK_OVERFLOW = 0,
    WASM_TRAP_MEMORY_OUT_OF_BOUNDS = 1,
    WASM_TRAP_HEAP_MISALIGNED = 2,
    WASM_TRAP_TABLE_OUT_OF_BOUNDS = 3,
    WASM_TRAP_INDIRECT_CALL_TO_NULL = 4,
    WASM_TRAP_BAD_SIGNATURE = 5,
    WASM_TRAP_INTEGER_OVERFLOW = 6,
    WASM_TRAP_INTEGER_DIVISION_BY_ZERO = 7,
    WASM_TRAP_BAD_CONVERSION_TO_INTEGER = 8,
    WASM_TRAP_UNREACHABLE = 9,
    WASM_TRAP_INTERRUPT = 10,
    WASM_TRAP_OUT_OF_FUEL = 11,
    WASM_TRAP_NULL_REFERENCE = 13,
};

/* wasmtime's message for a trap ("wasm trap: " + this). */
static inline const char *wasm_trap_message(int trap) {
    switch (trap) {
    case WASM_TRAP_STACK_OVERFLOW: return "call stack exhausted";
    case WASM_TRAP_MEMORY_OUT_OF_BOUNDS: return "out of bounds memory access";
    case WASM_TRAP_HEAP_MISALIGNED: return "unaligned atomic";
    case WASM_TRAP_TABLE_OUT_OF_BOUNDS: return "undefined element: out of bounds table access";
    case WASM_TRAP_INDIRECT_CALL_TO_NULL: return "uninitialized element";
    case WASM_TRAP_BAD_SIGNATURE: return "indirect call type mismatch";
    case WASM_TRAP_INTEGER_OVERFLOW: return "integer overflow";
    case WASM_TRAP_INTEGER_DIVISION_BY_ZERO: return "integer divide by zero";
    case WASM_TRAP_BAD_CONVERSION_TO_INTEGER: return "invalid conversion to integer";
    case WASM_TRAP_UNREACHABLE: return "wasm `unreachable` instruction executed";
    case WASM_TRAP_INTERRUPT: return "interrupt";
    case WASM_TRAP_OUT_OF_FUEL: return "all fuel consumed by WebAssembly";
    case WASM_TRAP_NULL_REFERENCE: return "null reference";
    default: return "trap";
    }
}

#define WASM_CANON_F32 0x7fc00000u
#define WASM_CANON_F64 0x7ff8000000000000ull

static inline uint32_t wasm_f32_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
static inline uint64_t wasm_f64_bits(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}
static inline float wasm_f32_from(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static inline double wasm_f64_from(uint64_t u) {
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static inline float wasm_canon32(float f) { return f != f ? wasm_f32_from(WASM_CANON_F32) : f; }
static inline double wasm_canon64(double d) { return d != d ? wasm_f64_from(WASM_CANON_F64) : d; }

/* ---- integers ---- */

static inline uint32_t wasm_i32_clz(uint32_t x) { return x ? (uint32_t)__builtin_clz(x) : 32; }
static inline uint32_t wasm_i32_ctz(uint32_t x) { return x ? (uint32_t)__builtin_ctz(x) : 32; }
static inline uint32_t wasm_i32_popcnt(uint32_t x) { return (uint32_t)__builtin_popcount(x); }
static inline uint64_t wasm_i64_clz(uint64_t x) { return x ? (uint64_t)__builtin_clzll(x) : 64; }
static inline uint64_t wasm_i64_ctz(uint64_t x) { return x ? (uint64_t)__builtin_ctzll(x) : 64; }
static inline uint64_t wasm_i64_popcnt(uint64_t x) { return (uint64_t)__builtin_popcountll(x); }
static inline uint32_t wasm_i32_rotl(uint32_t x, uint32_t k) { k &= 31; return k ? x << k | x >> (32 - k) : x; }
static inline uint32_t wasm_i32_rotr(uint32_t x, uint32_t k) { k &= 31; return k ? x >> k | x << (32 - k) : x; }
static inline uint64_t wasm_i64_rotl(uint64_t x, uint64_t k) { k &= 63; return k ? x << k | x >> (64 - k) : x; }
static inline uint64_t wasm_i64_rotr(uint64_t x, uint64_t k) { k &= 63; return k ? x >> k | x << (64 - k) : x; }
static inline uint32_t wasm_i32_shr_s(uint32_t x, uint32_t k) {
    k &= 31;
    return (x & 0x80000000u) ? ~(~x >> k) : x >> k;
}
static inline uint64_t wasm_i64_shr_s(uint64_t x, uint64_t k) {
    k &= 63;
    return (x & 0x8000000000000000ull) ? ~(~x >> k) : x >> k;
}

static inline int wasm_i32_div_s(uint32_t a, uint32_t b, uint32_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    if (a == 0x80000000u && b == 0xffffffffu) return WASM_TRAP_INTEGER_OVERFLOW;
    *r = (uint32_t)((int32_t)a / (int32_t)b);
    return WASM_NUM_OK;
}
static inline int wasm_i32_div_u(uint32_t a, uint32_t b, uint32_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    *r = a / b;
    return WASM_NUM_OK;
}
static inline int wasm_i32_rem_s(uint32_t a, uint32_t b, uint32_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    *r = b == 0xffffffffu ? 0 : (uint32_t)((int32_t)a % (int32_t)b);
    return WASM_NUM_OK;
}
static inline int wasm_i32_rem_u(uint32_t a, uint32_t b, uint32_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    *r = a % b;
    return WASM_NUM_OK;
}
static inline int wasm_i64_div_s(uint64_t a, uint64_t b, uint64_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    if (a == 0x8000000000000000ull && b == ~0ull) return WASM_TRAP_INTEGER_OVERFLOW;
    *r = (uint64_t)((int64_t)a / (int64_t)b);
    return WASM_NUM_OK;
}
static inline int wasm_i64_div_u(uint64_t a, uint64_t b, uint64_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    *r = a / b;
    return WASM_NUM_OK;
}
static inline int wasm_i64_rem_s(uint64_t a, uint64_t b, uint64_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    *r = b == ~0ull ? 0 : (uint64_t)((int64_t)a % (int64_t)b);
    return WASM_NUM_OK;
}
static inline int wasm_i64_rem_u(uint64_t a, uint64_t b, uint64_t *r) {
    if (b == 0) return WASM_TRAP_INTEGER_DIVISION_BY_ZERO;
    *r = a % b;
    return WASM_NUM_OK;
}

/* ---- floats ---- */

static inline float wasm_f32_min(float a, float b) {
    if (a != a || b != b) return wasm_f32_from(WASM_CANON_F32);
    if (a == b) return wasm_f32_from(wasm_f32_bits(a) | wasm_f32_bits(b)); /* min(-0, +0) = -0 */
    return a < b ? a : b;
}
static inline float wasm_f32_max(float a, float b) {
    if (a != a || b != b) return wasm_f32_from(WASM_CANON_F32);
    if (a == b) return wasm_f32_from(wasm_f32_bits(a) & wasm_f32_bits(b)); /* max(-0, +0) = +0 */
    return a > b ? a : b;
}
static inline double wasm_f64_min(double a, double b) {
    if (a != a || b != b) return wasm_f64_from(WASM_CANON_F64);
    if (a == b) return wasm_f64_from(wasm_f64_bits(a) | wasm_f64_bits(b));
    return a < b ? a : b;
}
static inline double wasm_f64_max(double a, double b) {
    if (a != a || b != b) return wasm_f64_from(WASM_CANON_F64);
    if (a == b) return wasm_f64_from(wasm_f64_bits(a) & wasm_f64_bits(b));
    return a > b ? a : b;
}

static inline float wasm_f32_add(float a, float b) { return wasm_canon32(a + b); }
static inline float wasm_f32_sub(float a, float b) { return wasm_canon32(a - b); }
static inline float wasm_f32_mul(float a, float b) { return wasm_canon32(a * b); }
static inline float wasm_f32_div(float a, float b) { return wasm_canon32(a / b); }
static inline float wasm_f32_sqrt(float a) { return wasm_canon32(sqrtf(a)); }
static inline float wasm_f32_ceil(float a) { return wasm_canon32(ceilf(a)); }
static inline float wasm_f32_floor(float a) { return wasm_canon32(floorf(a)); }
static inline float wasm_f32_trunc(float a) { return wasm_canon32(truncf(a)); }
/* Round to nearest, ties to even (the default rounding mode, which meshd never changes). */
static inline float wasm_f32_nearest(float a) { return wasm_canon32(nearbyintf(a)); }
static inline double wasm_f64_add(double a, double b) { return wasm_canon64(a + b); }
static inline double wasm_f64_sub(double a, double b) { return wasm_canon64(a - b); }
static inline double wasm_f64_mul(double a, double b) { return wasm_canon64(a * b); }
static inline double wasm_f64_div(double a, double b) { return wasm_canon64(a / b); }
static inline double wasm_f64_sqrt(double a) { return wasm_canon64(sqrt(a)); }
static inline double wasm_f64_ceil(double a) { return wasm_canon64(ceil(a)); }
static inline double wasm_f64_floor(double a) { return wasm_canon64(floor(a)); }
static inline double wasm_f64_trunc(double a) { return wasm_canon64(trunc(a)); }
static inline double wasm_f64_nearest(double a) { return wasm_canon64(nearbyint(a)); }
static inline float wasm_f32_demote(double a) { return wasm_canon32((float)a); }
static inline double wasm_f64_promote(float a) { return wasm_canon64((double)a); }

static inline uint32_t wasm_f32_neg(uint32_t a) { return a ^ 0x80000000u; }
static inline uint32_t wasm_f32_abs(uint32_t a) { return a & 0x7fffffffu; }
static inline uint32_t wasm_f32_copysign(uint32_t a, uint32_t b) { return (a & 0x7fffffffu) | (b & 0x80000000u); }
static inline uint64_t wasm_f64_neg(uint64_t a) { return a ^ 0x8000000000000000ull; }
static inline uint64_t wasm_f64_abs(uint64_t a) { return a & 0x7fffffffffffffffull; }
static inline uint64_t wasm_f64_copysign(uint64_t a, uint64_t b) {
    return (a & 0x7fffffffffffffffull) | (b & 0x8000000000000000ull);
}

/* ---- float to integer: trapping (NaN: bad conversion; out of range: overflow) ---- */

#define WASM_TRUNC(name, F, T, lo_ok, hi_ok)                                                                 \
    static inline int name(F x, T *r) {                                                                       \
        if (x != x) return WASM_TRAP_BAD_CONVERSION_TO_INTEGER;                                               \
        if (!(lo_ok) || !(hi_ok)) return WASM_TRAP_INTEGER_OVERFLOW;                                          \
        *r = (T)x;                                                                                            \
        return WASM_NUM_OK;                                                                                   \
    }
WASM_TRUNC(wasm_i32_trunc_f32_s_, float, int32_t, x >= -2147483648.0f, x < 2147483648.0f)
WASM_TRUNC(wasm_i32_trunc_f32_u_, float, uint32_t, x > -1.0f, x < 4294967296.0f)
WASM_TRUNC(wasm_i32_trunc_f64_s_, double, int32_t, x > -2147483649.0, x < 2147483648.0)
WASM_TRUNC(wasm_i32_trunc_f64_u_, double, uint32_t, x > -1.0, x < 4294967296.0)
WASM_TRUNC(wasm_i64_trunc_f32_s_, float, int64_t, x >= -9223372036854775808.0f, x < 9223372036854775808.0f)
WASM_TRUNC(wasm_i64_trunc_f32_u_, float, uint64_t, x > -1.0f, x < 18446744073709551616.0f)
WASM_TRUNC(wasm_i64_trunc_f64_s_, double, int64_t, x >= -9223372036854775808.0, x < 9223372036854775808.0)
WASM_TRUNC(wasm_i64_trunc_f64_u_, double, uint64_t, x > -1.0, x < 18446744073709551616.0)
#undef WASM_TRUNC

static inline int wasm_i32_trunc_f32_s(float x, uint32_t *r) {
    int32_t v = 0;
    int t = wasm_i32_trunc_f32_s_(x, &v);
    *r = (uint32_t)v;
    return t;
}
static inline int wasm_i32_trunc_f64_s(double x, uint32_t *r) {
    int32_t v = 0;
    int t = wasm_i32_trunc_f64_s_(x, &v);
    *r = (uint32_t)v;
    return t;
}
static inline int wasm_i64_trunc_f32_s(float x, uint64_t *r) {
    int64_t v = 0;
    int t = wasm_i64_trunc_f32_s_(x, &v);
    *r = (uint64_t)v;
    return t;
}
static inline int wasm_i64_trunc_f64_s(double x, uint64_t *r) {
    int64_t v = 0;
    int t = wasm_i64_trunc_f64_s_(x, &v);
    *r = (uint64_t)v;
    return t;
}
#define wasm_i32_trunc_f32_u wasm_i32_trunc_f32_u_
#define wasm_i32_trunc_f64_u wasm_i32_trunc_f64_u_
#define wasm_i64_trunc_f32_u wasm_i64_trunc_f32_u_
#define wasm_i64_trunc_f64_u wasm_i64_trunc_f64_u_

/* ---- float to integer: saturating (NaN: 0; out of range: the nearest bound) ---- */

static inline uint32_t wasm_i32_trunc_sat_f32_s(float x) {
    if (x != x) return 0;
    if (x < -2147483648.0f) return 0x80000000u;
    if (x >= 2147483648.0f) return 0x7fffffffu;
    return (uint32_t)(int32_t)x;
}
static inline uint32_t wasm_i32_trunc_sat_f32_u(float x) {
    if (x != x || x <= -1.0f) return 0;
    if (x >= 4294967296.0f) return 0xffffffffu;
    return (uint32_t)x;
}
static inline uint32_t wasm_i32_trunc_sat_f64_s(double x) {
    if (x != x) return 0;
    if (x <= -2147483649.0) return 0x80000000u;
    if (x >= 2147483648.0) return 0x7fffffffu;
    return (uint32_t)(int32_t)x;
}
static inline uint32_t wasm_i32_trunc_sat_f64_u(double x) {
    if (x != x || x <= -1.0) return 0;
    if (x >= 4294967296.0) return 0xffffffffu;
    return (uint32_t)x;
}
static inline uint64_t wasm_i64_trunc_sat_f32_s(float x) {
    if (x != x) return 0;
    if (x < -9223372036854775808.0f) return 0x8000000000000000ull;
    if (x >= 9223372036854775808.0f) return 0x7fffffffffffffffull;
    return (uint64_t)(int64_t)x;
}
static inline uint64_t wasm_i64_trunc_sat_f32_u(float x) {
    if (x != x || x <= -1.0f) return 0;
    if (x >= 18446744073709551616.0f) return ~0ull;
    return (uint64_t)x;
}
static inline uint64_t wasm_i64_trunc_sat_f64_s(double x) {
    if (x != x) return 0;
    if (x < -9223372036854775808.0) return 0x8000000000000000ull;
    if (x >= 9223372036854775808.0) return 0x7fffffffffffffffull;
    return (uint64_t)(int64_t)x;
}
static inline uint64_t wasm_i64_trunc_sat_f64_u(double x) {
    if (x != x || x <= -1.0) return 0;
    if (x >= 18446744073709551616.0) return ~0ull;
    return (uint64_t)x;
}

#endif
