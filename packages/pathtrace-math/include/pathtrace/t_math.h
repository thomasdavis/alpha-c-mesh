/* Template (no include guard: included once per instantiation; see pathtrace/real.h): programs/
 * pathtrace's vector math, sqrt and cis over the instantiation's float operations. */
#include "pathtrace/real.h"

#ifndef PT_API
#define PT_API static inline
#endif

static inline pt_v PT_FN(v3)(float x, float y, float z) {
    pt_v v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
static inline float PT_FN(ldr)(const uint32_t *sc, size_t i) { return PT_K(sc[i]); }
static inline pt_v PT_FN(ldv)(const uint32_t *sc, size_t i) {
    return PT_FN(v3)(PT_K(sc[i]), PT_K(sc[i + 1]), PT_K(sc[i + 2]));
}
static inline pt_v PT_FN(vadd)(pt_v a, pt_v b) { return PT_FN(v3)(PT_ADD(a.x, b.x), PT_ADD(a.y, b.y), PT_ADD(a.z, b.z)); }
static inline pt_v PT_FN(vsub)(pt_v a, pt_v b) { return PT_FN(v3)(PT_SUB(a.x, b.x), PT_SUB(a.y, b.y), PT_SUB(a.z, b.z)); }
static inline pt_v PT_FN(vmul)(pt_v a, pt_v b) { return PT_FN(v3)(PT_MUL(a.x, b.x), PT_MUL(a.y, b.y), PT_MUL(a.z, b.z)); }
static inline pt_v PT_FN(vscale)(pt_v a, float s) { return PT_FN(v3)(PT_MUL(a.x, s), PT_MUL(a.y, s), PT_MUL(a.z, s)); }
static inline pt_v PT_FN(vneg)(pt_v a) { return PT_FN(v3)(PT_NEG(a.x), PT_NEG(a.y), PT_NEG(a.z)); }
/* (a.x b.x + a.y b.y) + a.z b.z: never a builtin dot (which may fuse or reorder). */
static inline float PT_FN(dot)(pt_v a, pt_v b) {
    return PT_ADD(PT_ADD(PT_MUL(a.x, b.x), PT_MUL(a.y, b.y)), PT_MUL(a.z, b.z));
}
static inline pt_v PT_FN(cross)(pt_v a, pt_v b) {
    return PT_FN(v3)(PT_SUB(PT_MUL(a.y, b.z), PT_MUL(a.z, b.y)), PT_SUB(PT_MUL(a.z, b.x), PT_MUL(a.x, b.z)),
                     PT_SUB(PT_MUL(a.x, b.y), PT_MUL(a.y, b.x)));
}
static inline pt_v PT_FN(zero3)(void) { return PT_FN(v3)(PT_K(PT_C_ZERO), PT_K(PT_C_ZERO), PT_K(PT_C_ZERO)); }

/* sqrt by Newton from an integer first guess: three steps of y = (y + x / y) / 2 (about 1 ulp; the
 * same bits on every backend). x <= 0 or NaN gives 0. */
PT_API float PT_FN(sqrt)(float x) {
    if (!PT_GT(x, PT_K(PT_C_ZERO))) return PT_K(PT_C_ZERO);
    float y = PT_K(0x1fbd1df5u + (pt_to_bits(x) >> 1));
    float h = PT_K(PT_C_HALF);
    y = PT_MUL(h, PT_ADD(y, PT_DIV(x, y)));
    y = PT_MUL(h, PT_ADD(y, PT_DIV(x, y)));
    y = PT_MUL(h, PT_ADD(y, PT_DIV(x, y)));
    return y;
}

static inline pt_v PT_FN(normalize)(pt_v a) {
    float inv = PT_DIV(PT_K(PT_C_ONE), PT_FN(sqrt)(PT_FN(dot)(a, a)));
    return PT_FN(vscale)(a, inv);
}

/* (cos, sin) of a quarter turn's quadrant h >> 30 plus the angle of h's next 24 bits within it:
 * 2 pi h / 2^32, to 2^-26 of a turn. */
PT_API void PT_FN(cis)(uint32_t h, float *cos_out, float *sin_out) {
    float t = PT_MUL(PT_FROM_U32((h >> 6) & 0x00ffffffu), PT_K(PT_C_U24));
    float a = PT_MUL(t, PT_K(PT_C_HALF_PI));
    float a2 = PT_MUL(a, a);
    /* The Rust's nested Horner expressions, innermost first (the same operations in the same order). */
    float p = PT_ADD(PT_K(pt_c_sin[3]), PT_MUL(a2, PT_K(pt_c_sin[4])));
    p = PT_ADD(PT_K(pt_c_sin[2]), PT_MUL(a2, p));
    p = PT_ADD(PT_K(pt_c_sin[1]), PT_MUL(a2, p));
    p = PT_ADD(PT_K(pt_c_sin[0]), PT_MUL(a2, p));
    float s = PT_MUL(a, PT_ADD(PT_K(PT_C_ONE), PT_MUL(a2, p)));
    float q = PT_ADD(PT_K(pt_c_cos[4]), PT_MUL(a2, PT_K(pt_c_cos[5])));
    q = PT_ADD(PT_K(pt_c_cos[3]), PT_MUL(a2, q));
    q = PT_ADD(PT_K(pt_c_cos[2]), PT_MUL(a2, q));
    q = PT_ADD(PT_K(pt_c_cos[1]), PT_MUL(a2, q));
    q = PT_ADD(PT_K(pt_c_cos[0]), PT_MUL(a2, q));
    float c = PT_ADD(PT_K(PT_C_ONE), PT_MUL(a2, q));
    switch (h >> 30) {
    case 0: *cos_out = c, *sin_out = s; break;
    case 1: *cos_out = PT_NEG(s), *sin_out = c; break;
    case 2: *cos_out = PT_NEG(c), *sin_out = PT_NEG(s); break;
    default: *cos_out = s, *sin_out = PT_NEG(c); break;
    }
}
