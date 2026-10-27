/* GPT-2's scalar functions in Z (mesh-ref gpt2.rs `exp_poly`, `tanh_poly`, `gelu_tanh`, `sqrt_newton`). */
#include "mesh/ref/gpt2_scalar.h"

#include "mesh/ftz.h"

const uint32_t mesh_gpt2_exp_poly_coeffs[11] = {0x3493f27e, 0x3638ef1d, 0x37d00d01, 0x39500d01,
                                                0x3ab60b61, 0x3c088889, 0x3d2aaaab, 0x3e2aaaab,
                                                0x3f000000, 0x3f800000, 0x3f800000};

#define K(bits) mesh_ftz_from_bits(bits)

float mesh_gpt2_exp_poly(float x) {
    /* Plain f32 compares on the raw value, as the Rust's `x.0 < -87.0`. */
    if (x < -87.0f) return 0.0f;
    if (x > 88.0f) return K(0x7f800000u);
    float t = mesh_ftz_mul(x, K(MESH_GPT2_LOG2E_BITS));
    float s = mesh_ftz_ge(t, 0.0f) ? mesh_ftz_add(t, 0.5f) : mesh_ftz_sub(t, 0.5f);
    int32_t k = mesh_ftz_is_nan(s) ? 0 : (int32_t)s; /* cvt.rzi: truncation (|s| < 128); Rust's `as` maps NaN to 0 */
    float kf = mesh_ftz_from_i32(k);
    float r = mesh_ftz_sub(mesh_ftz_sub(x, mesh_ftz_mul(kf, K(MESH_GPT2_LN2_HI_BITS))), mesh_ftz_mul(kf, K(MESH_GPT2_LN2_LO_BITS)));
    float p = K(mesh_gpt2_exp_poly_coeffs[0]);
    for (int i = 1; i < 11; i++) p = mesh_ftz_add(mesh_ftz_mul(p, r), K(mesh_gpt2_exp_poly_coeffs[i]));
    int32_t k1 = k / 2, k2 = k - k1;
    return mesh_ftz_mul(mesh_ftz_mul(p, K((uint32_t)(k1 + 127) << 23)), K((uint32_t)(k2 + 127) << 23));
}

float mesh_gpt2_tanh_poly(float x) {
    if (x > 10.0f) return 1.0f;
    if (x < -10.0f) return -1.0f;
    return mesh_ftz_sub(1.0f, mesh_ftz_div(2.0f, mesh_ftz_add(mesh_gpt2_exp_poly(mesh_ftz_mul(x, 2.0f)), 1.0f)));
}

float mesh_gpt2_gelu_tanh(float x) {
    float cube = mesh_ftz_mul(mesh_ftz_mul(mesh_ftz_mul(K(MESH_GPT2_GELU_A_BITS), x), x), x);
    float inner = mesh_ftz_mul(K(MESH_GPT2_GELU_C_BITS), mesh_ftz_add(x, cube));
    return mesh_ftz_mul(mesh_ftz_mul(0.5f, x), mesh_ftz_add(1.0f, mesh_gpt2_tanh_poly(inner)));
}

float mesh_gpt2_sqrt_newton(float a) {
    float y = K((mesh_ftz_bits(a) >> 1) + 0x1fbd1df5u);
    for (int i = 0; i < 4; i++) y = mesh_ftz_mul(0.5f, mesh_ftz_add(y, mesh_ftz_div(a, y)));
    return y;
}
