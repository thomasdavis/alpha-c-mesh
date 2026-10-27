/* The numeric semantics: the spec's edge cases and wasmtime's canonical NaNs. */
#include "mesh/check.h"
#include "wasm/numeric.h"

static uint32_t b32(float f) { return wasm_f32_bits(f); }
static uint64_t b64(double d) { return wasm_f64_bits(d); }

int main(void) {
    float nanp = wasm_f32_from(0x7fa00001u), nann = wasm_f32_from(0xffc12345u);
    /* Arithmetic NaNs are canonical, whatever the inputs' payloads. */
    CHECK_U(b32(wasm_f32_add(nanp, 1.0f)), WASM_CANON_F32);
    CHECK_U(b32(wasm_f32_mul(nann, nanp)), WASM_CANON_F32);
    CHECK_U(b32(wasm_f32_sub(INFINITY, INFINITY)), WASM_CANON_F32);
    CHECK_U(b32(wasm_f32_sqrt(-1.0f)), WASM_CANON_F32);
    CHECK_U(b32(wasm_f32_min(nann, 1.0f)), WASM_CANON_F32);
    CHECK_U(b32(wasm_f32_demote(wasm_f64_from(0xfff0000000000001ull))), WASM_CANON_F32);
    CHECK_U(b64(wasm_f64_promote(nann)), WASM_CANON_F64);
    CHECK_U(b64(wasm_f64_div(0.0, 0.0)), WASM_CANON_F64);
    /* Bit operations keep payloads. */
    CHECK_U(wasm_f32_neg(0x7fa00001u), 0xffa00001u);
    CHECK_U(wasm_f32_abs(0xffc12345u), 0x7fc12345u);
    CHECK_U(wasm_f32_copysign(0x7fa00001u, 0x80000000u), 0xffa00001u);
    /* min/max of zeros, and ordinary values. */
    CHECK_U(b32(wasm_f32_min(0.0f, -0.0f)), 0x80000000u);
    CHECK_U(b32(wasm_f32_max(-0.0f, 0.0f)), 0x00000000u);
    CHECK_U(b64(wasm_f64_min(-0.0, 0.0)), 0x8000000000000000ull);
    CHECK(wasm_f32_min(1.0f, 2.0f) == 1.0f);
    CHECK(wasm_f64_max(1.0, 2.0) == 2.0);
    /* nearest is ties-to-even; trunc, ceil, floor; -0 survives. */
    CHECK(wasm_f32_nearest(2.5f) == 2.0f);
    CHECK(wasm_f32_nearest(3.5f) == 4.0f);
    CHECK_U(b32(wasm_f32_nearest(-0.5f)), 0x80000000u);
    CHECK(wasm_f64_nearest(-2.5) == -2.0);
    CHECK_U(b32(wasm_f32_ceil(-0.5f)), 0x80000000u);
    CHECK(wasm_f64_floor(-0.5) == -1.0);
    CHECK(wasm_f32_trunc(-1.7f) == -1.0f);
    /* Integer division and remainder traps. */
    uint32_t r;
    uint64_t q;
    CHECK_I(wasm_i32_div_s(1, 0, &r), WASM_TRAP_INTEGER_DIVISION_BY_ZERO);
    CHECK_I(wasm_i32_div_s(0x80000000u, 0xffffffffu, &r), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(wasm_i32_rem_s(0x80000000u, 0xffffffffu, &r), WASM_NUM_OK);
    CHECK_U(r, 0);
    CHECK_I(wasm_i32_div_s((uint32_t)-7, 2, &r), WASM_NUM_OK);
    CHECK_U(r, (uint32_t)-3);
    CHECK_I(wasm_i32_rem_s((uint32_t)-7, 2, &r), WASM_NUM_OK);
    CHECK_U(r, (uint32_t)-1);
    CHECK_I(wasm_i64_div_s(0x8000000000000000ull, ~0ull, &q), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(wasm_i64_rem_u(5, 0, &q), WASM_TRAP_INTEGER_DIVISION_BY_ZERO);
    /* Bits. */
    CHECK_U(wasm_i32_clz(0), 32);
    CHECK_U(wasm_i32_ctz(0x80000000u), 31);
    CHECK_U(wasm_i64_popcnt(~0ull), 64);
    CHECK_U(wasm_i32_rotl(0x80000001u, 1), 3);
    CHECK_U(wasm_i32_rotr(1, 33), 0x80000000u);
    CHECK_U(wasm_i64_rotl(1, 64), 1);
    CHECK_U(wasm_i32_shr_s(0x80000000u, 31), 0xffffffffu);
    CHECK_U(wasm_i64_shr_s(0x8000000000000000ull, 64 + 4), 0xf800000000000000ull);
    /* Trapping truncation: NaN, the exact bounds. */
    CHECK_I(wasm_i32_trunc_f32_s(nanp, &r), WASM_TRAP_BAD_CONVERSION_TO_INTEGER);
    CHECK_I(wasm_i32_trunc_f32_s(2147483648.0f, &r), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(wasm_i32_trunc_f32_s(-2147483648.0f, &r), WASM_NUM_OK);
    CHECK_U(r, 0x80000000u);
    CHECK_I(wasm_i32_trunc_f64_s(-2147483648.9, &r), WASM_NUM_OK);
    CHECK_U(r, 0x80000000u);
    CHECK_I(wasm_i32_trunc_f64_s(-2147483649.0, &r), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(wasm_i32_trunc_f32_u(-0.9f, &r), WASM_NUM_OK);
    CHECK_U(r, 0);
    CHECK_I(wasm_i32_trunc_f32_u(-1.0f, &r), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(wasm_i32_trunc_f64_u(4294967295.9, &r), WASM_NUM_OK);
    CHECK_U(r, 0xffffffffu);
    CHECK_I(wasm_i64_trunc_f64_u(18446744073709551616.0, &q), WASM_TRAP_INTEGER_OVERFLOW);
    CHECK_I(wasm_i64_trunc_f32_s(-9223372036854775808.0f, &q), WASM_NUM_OK);
    CHECK_U(q, 0x8000000000000000ull);
    /* Saturating truncation. */
    CHECK_U(wasm_i32_trunc_sat_f32_s(nanp), 0);
    CHECK_U(wasm_i32_trunc_sat_f32_s(3e9f), 0x7fffffffu);
    CHECK_U(wasm_i32_trunc_sat_f32_s(-3e9f), 0x80000000u);
    CHECK_U(wasm_i32_trunc_sat_f64_u(-5.0), 0);
    CHECK_U(wasm_i32_trunc_sat_f64_u(1e10), 0xffffffffu);
    CHECK_U(wasm_i64_trunc_sat_f64_s(1e19), 0x7fffffffffffffffull);
    CHECK_U(wasm_i64_trunc_sat_f32_u(INFINITY), ~0ull);
    CHECK_U(wasm_i64_trunc_sat_f64_u(123.9), 123);
    CHECK_S(wasm_trap_message(WASM_TRAP_UNREACHABLE), "wasm `unreachable` instruction executed");
    return mesh_check_done();
}
