/* @ajax/pathtrace-math: the constants of programs/pathtrace as f32 bits (program.metal has the same),
 * the vector type, and bit casts. Freestanding. */
#ifndef PATHTRACE_CONSTS_H
#define PATHTRACE_CONSTS_H

#include <stdint.h>

#define PT_C_ONE 0x3f800000u
#define PT_C_HALF 0x3f000000u
#define PT_C_TWO 0x40000000u
#define PT_C_ZERO 0u
#define PT_C_U24 0x33800000u     /* 2^-24: a 24-bit integer to [0, 1) */
#define PT_C_HALF_PI 0x3fc90fdbu
#define PT_C_INV_PI 0x3ea2f983u
/* Ray offset and the nearest hit (1e-4), the far plane (1e30), the parallel tests, the shadow margin. */
#define PT_C_EPS 0x38d1b717u
#define PT_C_BIG 0x7149f2cau
#define PT_C_DET_EPS 0x3089705fu
#define PT_C_PAR_EPS 0x322bcc77u
#define PT_C_SHADOW_EPS 0x3a83126fu
/* Russian roulette's survival probability bounds (0.05, 0.95), from depth PT_RR_DEPTH. */
#define PT_C_RR_LO 0x3d4ccccdu
#define PT_C_RR_HI 0x3f733333u
#define PT_RR_DEPTH 3u
/* Hit ids: 0 none, 1 + i quad i, ID_SPH + i sphere i, ID_TRI + i triangle i. */
#define PT_ID_SPH 0x100u
#define PT_ID_TRI 0x200u

/* sin: a (1 + a2 (S1 + a2 (S2 + ..))), cos: 1 + a2 (C1 + a2 (C2 + ..)) (Taylor, |a| <= pi/2). */
static const uint32_t pt_c_sin[5] = {0xbe2aaaab, 0x3c088889, 0xb9500d01, 0x3638ef1d, 0xb2d7322b};
static const uint32_t pt_c_cos[6] = {0xbf000000, 0x3d2aaaab, 0xbab60b61, 0x37d00d01, 0xb493f27e, 0x310f76c7};
/* ACES filmic fit (Narkowicz): x (A x + B) / (x (C x + D) + E). */
static const uint32_t pt_c_aces[5] = {0x4020a3d7, 0x3cf5c28f, 0x401b851f, 0x3f170a3d, 0x3e0f5c29};

typedef struct {
    float x, y, z;
} pt_v;

typedef union {
    float f;
    uint32_t u;
} pt_pun;

static inline float pt_from_bits(uint32_t b) {
    pt_pun p;
    p.u = b;
    return p.f;
}

static inline uint32_t pt_to_bits(float x) {
    pt_pun p;
    p.f = x;
    return p.u;
}

#endif
