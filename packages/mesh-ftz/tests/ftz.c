/* mesh-ref ftz.rs's tests, then golden digests from the Rust over every class of input: the special
 * values crossed with each other, four sweeps of a million random pairs (any bits, tiny exponents,
 * products and quotients near the subnormal boundary), and f64 -> f32 converts. Each digest is
 * FNV-1a 64 over every operation's result bits, and the flushes counted must match too. */
#include "mesh/ftz.h"

#include <stdio.h>

static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const float SUB = 1e-40f; /* subnormal */
static const float MIN = 1.17549435e-38f;

static float f(uint32_t b) { return mesh_ftz_from_bits(b); }
static uint32_t bits(float x) { return mesh_ftz_bits(x); }

static void rust_inputs_and_results_flush_sign_preserving(void) {
    CHECK(bits(mesh_ftz_add(SUB, 0.0f)) == 0);
    CHECK(bits(mesh_ftz_add(-SUB, -0.0f)) == 0x80000000u);
    CHECK(mesh_ftz_mul(SUB, 1e30f) == 0.0f);
    CHECK(bits(mesh_ftz_mul(MIN, 0.5f)) == 0);
    CHECK(bits(mesh_ftz_mul(-MIN, 0.5f)) == 0x80000000u);
    CHECK(bits(mesh_ftz_sub(MIN, MIN * 1.5f)) == 0x80000000u);
    CHECK(mesh_ftz_div(1.0f, SUB) == f(0x7f800000u));
    CHECK(mesh_ftz_div(1.0f, -SUB) == f(0xff800000u));
    CHECK(mesh_ftz_eq(mesh_ftz_mul(MIN * 2.0f, 0.5f), MIN));
    CHECK(mesh_ftz_add(1.5f, 2.25f) == 3.75f);
    CHECK(bits(mesh_ftz_neg(-SUB)) == 0);
    CHECK(bits(mesh_ftz_abs(-SUB)) == 0);
}

static void rust_compares_and_converts_flush(void) {
    float nan = f(0x7fc00000u), inf = f(0x7f800000u);
    CHECK(mesh_ftz_eq(SUB, 0.0f) && mesh_ftz_eq(-SUB, 0.0f));
    CHECK(mesh_ftz_ge(-SUB, 0.0f) && mesh_ftz_le(SUB, 0.0f));
    CHECK(mesh_ftz_lt(SUB, MIN) && mesh_ftz_is_nan(nan));
    CHECK(mesh_ftz_to_f64(SUB) == 0.0);
    CHECK(bits(mesh_ftz_from_f64(1e-40)) == 0);
    CHECK(bits(mesh_ftz_from_f64(-1e-40)) == 0x80000000u);
    CHECK(mesh_ftz_from_f64(1e-30) == (float)1e-30);
    volatile float moved = SUB; /* moves keep the bits: only operations flush */
    CHECK(bits(moved) == bits(SUB));
    CHECK(mesh_ftz_is_nan(nan) && mesh_ftz_is_nan(f(0xffc00000u)) && !mesh_ftz_is_nan(inf) &&
          !mesh_ftz_is_nan(SUB));
    CHECK(mesh_ftz_ne(nan, nan) && mesh_ftz_cmp(nan, 1.0f) == MESH_FTZ_UNORDERED);
    CHECK(mesh_ftz_eq(-0.0f, 0.0f) && mesh_ftz_gt(1.0f, -SUB) && mesh_ftz_lt(-1.0f, 0.0f));
}

/* ---- golden digests (gold ftz, from the Rust at alpha-rust-mesh 00b526b) ---- */

static const uint32_t SPECIALS[44] = {
    0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x00400000, 0x80400000,
    0x00800000, 0x80800000, 0x00800001, 0x3f800000, 0xbf800000, 0x3f000000, 0x7f7fffff, 0xff7fffff,
    0x7f800000, 0xff800000, 0x7fc00000, 0xffc00000, 0x7f800001, 0x7fa00000, 0xffbfffff, 0x7fffffff,
    0x01000000, 0x0c000000, 0x33800000, 0x34000000, 0x4b800000, 0x5f000000, 0x1f800000, 0x20000000,
    0x00000002, 0x00100000, 0x3fc00000, 0x40490fdb, 0xc0490fdb, 0x2edbe6ff, 0x0d800000, 0x8d800000,
    0x00ffffff, 0x3f7fffff, 0x7effffff, 0x1fffffff,
};

static uint64_t fnv(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static uint32_t mix(uint32_t x) { /* mesh-ref block.rs lowbias32 */
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

static uint32_t gen(uint32_t m, uint32_t i) {
    uint32_t h = mix((i * 0x9e3779b9u) ^ (m * 0x85ebca6bu));
    uint32_t h2 = mix(h ^ 0x27d4eb2fu);
    switch (m) {
    case 0: return h;
    case 1: return (h & 0x80000000u) | ((h2 % 41) << 23) | (h & 0x007fffffu);
    case 2: return (h & 0x80000000u) | ((50 + h2 % 31) << 23) | (h & 0x007fffffu);
    default:
        if (i % 2 == 0) return (h & 0x80000000u) | ((1 + h2 % 40) << 23) | (h & 0x007fffffu);
        return (h & 0x80000000u) | ((120 + h2 % 41) << 23) | (h & 0x007fffffu);
    }
}

static uint64_t f64_bits(double d) {
    union {
        double d;
        uint64_t u;
    } p;
    p.d = d;
    return p.u;
}

static double f64_from(uint64_t u) {
    union {
        double d;
        uint64_t u;
    } p;
    p.u = u;
    return p.d;
}

static uint64_t ops(uint64_t h, float a, float b) {
    int32_t c = mesh_ftz_cmp(a, b);
    uint64_t cmp = c == MESH_FTZ_LESS ? 0 : c == MESH_FTZ_EQUAL ? 1 : c == MESH_FTZ_GREATER ? 2 : 3;
    uint64_t v[9] = {bits(mesh_ftz_add(a, b)), bits(mesh_ftz_sub(a, b)), bits(mesh_ftz_mul(a, b)),
                     bits(mesh_ftz_div(a, b)), bits(mesh_ftz_neg(a)),    bits(mesh_ftz_abs(a)),
                     f64_bits(mesh_ftz_to_f64(a)), cmp, mesh_ftz_is_nan(a)};
    for (int i = 0; i < 9; i++) h = fnv(h, v[i]);
    return h;
}

static void golden(const char *what, uint64_t got, uint64_t want, uint64_t flushes, uint64_t want_flushes) {
    if (got != want || flushes != want_flushes) {
        fprintf(stderr, "%s: digest %#018llx flushes %llu, want %#018llx flushes %llu\n", what,
                (unsigned long long)got, (unsigned long long)flushes, (unsigned long long)want,
                (unsigned long long)want_flushes);
        failures++;
    }
}

static void rust_golden(void) {
    uint64_t f0 = mesh_ftz_flushes(), h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 44; i++)
        for (int j = 0; j < 44; j++) h = ops(h, f(SPECIALS[i]), f(SPECIALS[j]));
    golden("specials", h, 0xbff860139a4dfbe2ull, mesh_ftz_flushes() - f0, 4664);

    static const uint64_t want[4] = {0x1a90a77d71d05624ull, 0xc547bacc65330903ull, 0xe7ef00233428a479ull,
                                     0x7cfe6dcde1a58f4full};
    static const uint64_t want_flushes[4] = {134486, 319309, 396123, 337035};
    for (uint32_t m = 0; m < 4; m++) {
        f0 = mesh_ftz_flushes();
        h = 0xcbf29ce484222325ull;
        for (uint32_t i = 0; i < 1000000; i++) h = ops(h, f(gen(m, 2 * i)), f(gen(m, 2 * i + 1)));
        char what[16];
        snprintf(what, sizeof what, "sweep %u", m);
        golden(what, h, want[m], mesh_ftz_flushes() - f0, want_flushes[m]);
    }

    f0 = mesh_ftz_flushes();
    h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 1000000; i++) {
        uint32_t hi = mix(i ^ 0x5bd1e995u), lo = mix(hi ^ i);
        uint64_t e = 1023 - 160 + hi % 200;
        uint64_t b = (((uint64_t)hi & 0x80000000u) << 32) | (e << 52) | (((uint64_t)hi & 0xfffff) << 32) | lo;
        h = fnv(h, bits(mesh_ftz_from_f64(f64_from(b))));
    }
    const double ds[12] = {1e-40, -1e-40, 1e-30, 1e300, -1e300, f64_from(0x7ff8000000000000ull),
                           f64_from(0xfff8000000000000ull), f64_from(0x7ff0000000000001ull),
                           1.1754942106924411e-38, 1.1754943508222875e-38, 0.0, -0.0};
    for (int i = 0; i < 12; i++) h = fnv(h, bits(mesh_ftz_from_f64(ds[i])));
    golden("from_f64", h, 0xfafb102caf21f6dcull, mesh_ftz_flushes() - f0, 120410);
}

int main(void) {
    rust_inputs_and_results_flush_sign_preserving();
    rust_compares_and_converts_flush();
    rust_golden();
    if (failures) fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
