/* gpt2.rs's constants_are_their_values and scalar_functions_are_close (references in double, computed
 * here without libm), then a golden digest from the Rust over 300 000 inputs of each function. */
#include "mesh/ref/gpt2_scalar.h"

#include "mesh/ftz.h"

#include <stdio.h>

static int failures;

static double dabs(double x) { return x < 0 ? -x : x; }

/* exp by range reduction to |r| <= ln2/2 and a long Taylor series. */
static double dexp(double x) {
    const double ln2 = 0.6931471805599453;
    int k = (int)(x / ln2 + (x < 0 ? -0.5 : 0.5));
    double r = x - k * ln2, term = 1, sum = 1;
    for (int n = 1; n < 30; n++) term *= r / n, sum += term;
    while (k > 0) sum *= 2, k--;
    while (k < 0) sum /= 2, k++;
    return sum;
}

static double dtanh(double x) { return 1 - 2 / (dexp(2 * x) + 1); }

static double dsqrt(double x) {
    double y = x > 1 ? x : 1;
    for (int i = 0; i < 200; i++) y = 0.5 * (y + x / y);
    return y;
}

static uint64_t fh(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

static uint32_t gen(uint32_t m, uint32_t i) {
    uint32_t h = mix((i * 0x9e3779b9u) ^ (m * 0x85ebca6bu));
    uint32_t h2 = mix(h ^ 0x27d4eb2fu);
    if (m == 0) return h;
    if (m == 1) return (h & 0x80000000u) | ((h2 % 41) << 23) | (h & 0x007fffffu);
    return (h & 0x80000000u) | ((50 + h2 % 31) << 23) | (h & 0x007fffffu);
}

int main(void) {
    /* constants_are_their_values */
    if (mesh_ftz_from_bits(MESH_GPT2_LN_EPS_BITS) != 1e-5f || mesh_ftz_from_bits(MESH_GPT2_GELU_C_BITS) != 0.7978846f ||
        mesh_ftz_from_bits(MESH_GPT2_GELU_A_BITS) != 0.044715f || mesh_ftz_from_bits(MESH_GPT2_LOG2E_BITS) != 1.442695f ||
        mesh_ftz_from_bits(MESH_GPT2_LN2_HI_BITS) != 0.69314575f ||
        mesh_ftz_from_bits(MESH_GPT2_LN2_LO_BITS) != 1.4286068e-6f || mesh_ftz_from_bits(MESH_GPT2_ATTN_SCALE_BITS) != 0.125f)
        failures++;
    const float want[11] = {1.0f / 3628800.0f, 1.0f / 362880.0f, 1.0f / 40320.0f, 1.0f / 5040.0f, 1.0f / 720.0f,
                            1.0f / 120.0f,     1.0f / 24.0f,     1.0f / 6.0f,     0.5f,           1.0f, 1.0f};
    for (int i = 0; i < 11; i++)
        if (mesh_ftz_from_bits(mesh_gpt2_exp_poly_coeffs[i]) != want[i]) failures++;

    /* scalar_functions_are_close */
    for (int i = -400; i <= 400; i++) {
        float x = (float)i / 8.0f;
        if (x > -87.0f && x < 60.0f) {
            double e = mesh_gpt2_exp_poly(x);
            if (dabs(e / dexp(x) - 1.0) >= 4e-7) fprintf(stderr, "exp %g: %g\n", x, e), failures++;
        }
        double t = mesh_gpt2_tanh_poly(x / 8.0f);
        if (dabs(t - dtanh((double)x / 8.0)) >= 3e-7) fprintf(stderr, "tanh %g\n", x), failures++;
    }
    const float as[7] = {1e-5f, 0.01f, 0.5f, 1.0f, 2.0f, 1234.5f, 3e7f};
    for (int i = 0; i < 7; i++) {
        double s = mesh_gpt2_sqrt_newton(as[i]);
        if (dabs(s / dsqrt(as[i]) - 1.0) >= 2e-7) fprintf(stderr, "sqrt %g\n", as[i]), failures++;
    }
    if (mesh_gpt2_exp_poly(0.0f) != 1.0f || mesh_gpt2_exp_poly(-100.0f) != 0.0f) failures++;

    /* golden: gold gpt2 "scalars" */
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 300000; i++) {
        float x = mesh_ftz_from_bits(gen(i % 3, i));
        float y = ((float)i - 150000.0f) / 1000.0f;
        float v[7] = {mesh_gpt2_exp_poly(x),  mesh_gpt2_exp_poly(y), mesh_gpt2_tanh_poly(x), mesh_gpt2_tanh_poly(y),
                      mesh_gpt2_gelu_tanh(x), mesh_gpt2_gelu_tanh(y),
                      mesh_gpt2_sqrt_newton(mesh_ftz_from_bits(gen(i % 3, i) & 0x7fffffffu))};
        for (int j = 0; j < 7; j++) h = fh(h, mesh_ftz_bits(v[j]));
    }
    if (h != 0xcd832a1d2b3bc7b0ull) fprintf(stderr, "scalars %#018llx\n", (unsigned long long)h), failures++;
    return failures != 0;
}
