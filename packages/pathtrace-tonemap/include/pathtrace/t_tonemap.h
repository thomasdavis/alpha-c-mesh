/* Template (included once per instantiation after t_math.h; see pathtrace/real.h): a linear sum of
 * samples to an 8-bit sRGB pixel. */
#include "pathtrace/real.h"
#include "pathtrace/scene.h"
#include "pathtrace/srgb.h"

static inline float PT_FN(curve)(float m, uint32_t op) {
    float one = PT_K(PT_C_ONE);
    if (op == PT_TONEMAP_REINHARD) return PT_DIV(m, PT_ADD(one, m));
    return PT_DIV(PT_MUL(m, PT_ADD(PT_MUL(PT_K(pt_c_aces[0]), m), PT_K(pt_c_aces[1]))),
                  PT_ADD(PT_MUL(m, PT_ADD(PT_MUL(PT_K(pt_c_aces[2]), m), PT_K(pt_c_aces[3]))), PT_K(pt_c_aces[4])));
}

/* The sRGB code of a linear value: the largest k with x >= pt_srgb_t[k] (binary search; NaN and
 * x < pt_srgb_t[1]: 0). */
PT_API uint32_t PT_FN(quantize)(float x) {
    uint32_t lo = 0;
    for (uint32_t step = 128; step > 0; step >>= 1)
        if (PT_GE(x, PT_K(pt_srgb_t[lo + step]))) lo += step;
    return lo;
}

/* An accumulated pixel (the sum of n samples) as 0xffBBGGRR. */
PT_API uint32_t PT_FN(tonemap)(pt_v acc, uint32_t n, float exposure, uint32_t op) {
    float nf = PT_FROM_U32(n);
    uint32_t r = PT_FN(quantize)(PT_FN(curve)(PT_MUL(PT_DIV(acc.x, nf), exposure), op));
    uint32_t g = PT_FN(quantize)(PT_FN(curve)(PT_MUL(PT_DIV(acc.y, nf), exposure), op));
    uint32_t b = PT_FN(quantize)(PT_FN(curve)(PT_MUL(PT_DIV(acc.z, nf), exposure), op));
    return r | g << 8 | b << 16 | 0xff000000u;
}
