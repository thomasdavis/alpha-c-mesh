/* Template (included once per instantiation after t_math.h and t_intersect.h; see pathtrace/real.h):
 * one camera sample's radiance for pixel (x, y), sample s, and a pixel's pass (its samples
 * p S .. p S + S - 1 summed in order from +0). */
#include "pathtrace/real.h"
#include "pathtrace/rng.h"
#include "pathtrace/scene.h"

/* A 24-bit integer to [0, 1). */
static inline float PT_FN(u01)(uint32_t h) { return PT_MUL(PT_FROM_U32(h >> 8), PT_K(PT_C_U24)); }

/* The hit's geometric normal (unit) and material. */
static inline pt_v PT_FN(surface)(const uint32_t *sc, const pt_layout *l, uint32_t id, pt_v h, uint32_t *mat) {
    if (id >= PT_ID_TRI) {
        size_t b = l->tri + PT_TRI_WORDS * (size_t)(id - PT_ID_TRI);
        *mat = sc[b + 12];
        return PT_FN(ldv)(sc, b + 9);
    }
    if (id >= PT_ID_SPH) {
        size_t b = l->sph + PT_SPH_WORDS * (size_t)(id - PT_ID_SPH);
        *mat = sc[b + 5];
        return PT_FN(vscale)(PT_FN(vsub)(h, PT_FN(ldv)(sc, b)), PT_FN(ldr)(sc, b + 4));
    }
    size_t b = l->quad + PT_QUAD_WORDS * (size_t)(id - 1);
    *mat = sc[b + 15];
    return PT_FN(ldv)(sc, b + 9);
}

/* An orthonormal basis around unit n (Duff et al. 2017), applied to (x, y, z). */
static inline pt_v PT_FN(onb)(pt_v n, float x, float y, float z) {
    float one = PT_K(PT_C_ONE);
    float sign = (pt_to_bits(n.z) >> 31) != 0 ? PT_NEG(one) : one;
    float a = PT_DIV(PT_NEG(one), PT_ADD(sign, n.z));
    float b = PT_MUL(PT_MUL(n.x, n.y), a);
    pt_v t = PT_FN(v3)(PT_ADD(one, PT_MUL(PT_MUL(PT_MUL(sign, n.x), n.x), a)), PT_MUL(sign, b), PT_MUL(PT_NEG(sign), n.x)); /* Rust's -sign * n.x is (-sign) * n.x */
    pt_v s = PT_FN(v3)(b, PT_ADD(sign, PT_MUL(PT_MUL(n.y, n.y), a)), PT_NEG(n.y));
    return PT_FN(v3)(PT_ADD(PT_ADD(PT_MUL(t.x, x), PT_MUL(s.x, y)), PT_MUL(n.x, z)),
                     PT_ADD(PT_ADD(PT_MUL(t.y, x), PT_MUL(s.y, y)), PT_MUL(n.y, z)),
                     PT_ADD(PT_ADD(PT_MUL(t.z, x), PT_MUL(s.z, y)), PT_MUL(n.z, z)));
}

static inline pt_v PT_FN(reflect)(pt_v d, pt_v n) {
    return PT_FN(vsub)(d, PT_FN(vscale)(n, PT_MUL(PT_K(PT_C_TWO), PT_FN(dot)(d, n))));
}

/* A NaN sample counts 0 (a bit test), and a large one `cl` (fireflies). */
static inline float PT_FN(clamp_sample)(float x, float cl) {
    if ((pt_to_bits(x) & 0x7fffffffu) > 0x7f800000u) return PT_K(PT_C_ZERO);
    return PT_GT(x, cl) ? cl : x;
}

/* One camera sample's radiance: pixel (x, y), sample s. */
PT_API pt_v PT_FN(sample)(const uint32_t *sc, const pt_layout *l, uint32_t x, uint32_t y, uint32_t s) {
    float zero = PT_K(PT_C_ZERO), one = PT_K(PT_C_ONE);
    uint32_t w = sc[PT_H_WIDTH], hgt = sc[PT_H_HEIGHT];
    uint32_t key = pt_sample_key(sc[PT_H_SEED], y * w + x, s);
    float fx = PT_DIV(PT_ADD(PT_FROM_U32(x), PT_FN(u01)(pt_rnd(key, 0))), PT_FROM_U32(w));
    float fy = PT_DIV(PT_ADD(PT_FROM_U32(y), PT_FN(u01)(pt_rnd(key, 1))), PT_FROM_U32(hgt));
    pt_v eye = PT_FN(ldv)(sc, PT_H_EYE);
    pt_v dir = PT_FN(vsub)(PT_FN(vadd)(PT_FN(vadd)(PT_FN(ldv)(sc, PT_H_CORNER), PT_FN(vscale)(PT_FN(ldv)(sc, PT_H_HORIZ), fx)),
                                       PT_FN(vscale)(PT_FN(ldv)(sc, PT_H_VERT), fy)),
                           eye);
    pt_v d = PT_FN(normalize)(dir);
    pt_v o = eye;
    pt_v tp = PT_FN(v3)(one, one, one);
    pt_v acc = PT_FN(zero3)();
    /* Emission counts on camera rays and after specular bounces; after a diffuse bounce the NEE
     * light's was already counted by its light sample. */
    int spec = 1;
    uint32_t depth = sc[PT_H_DEPTH];
    for (uint32_t b = 0; b < depth; b++) {
        uint32_t id;
        float t = PT_FN(intersect)(sc, l, o, d, PT_K(PT_C_BIG), &id);
        if (id == 0) break;
        pt_v h = PT_FN(vadd)(o, PT_FN(vscale)(d, t));
        uint32_t mat;
        pt_v n = PT_FN(surface)(sc, l, id, h, &mat);
        int front = PT_LT(PT_FN(dot)(d, n), zero);
        pt_v ns = front ? n : PT_FN(vneg)(n);
        size_t mb = l->mat + PT_MAT_WORDS * (size_t)mat;
        uint32_t kind = sc[mb];
        pt_v albedo = PT_FN(ldv)(sc, mb + 1);
        uint32_t dim = 2 + 8 * b;
        if (kind == PT_EMIT) {
            if (front && (spec || id != l->light + 1u)) acc = PT_FN(vadd)(acc, PT_FN(vmul)(tp, PT_FN(ldv)(sc, mb + 4)));
            break;
        }
        if (kind == PT_DIFFUSE) {
            if (l->light != PT_NO_LIGHT) {
                size_t lb = l->quad + PT_QUAD_WORDS * (size_t)l->light;
                pt_v lp = PT_FN(vadd)(PT_FN(vadd)(PT_FN(ldv)(sc, lb), PT_FN(vscale)(PT_FN(ldv)(sc, lb + 3), PT_FN(u01)(pt_rnd(key, dim)))),
                                      PT_FN(vscale)(PT_FN(ldv)(sc, lb + 6), PT_FN(u01)(pt_rnd(key, dim + 1))));
                pt_v wv = PT_FN(vsub)(lp, h);
                float dist2 = PT_FN(dot)(wv, wv);
                float dist = PT_FN(sqrt)(dist2);
                pt_v wl = PT_FN(vscale)(wv, PT_DIV(one, dist));
                float cs = PT_FN(dot)(ns, wl);
                float cl = PT_NEG(PT_FN(dot)(PT_FN(ldv)(sc, lb + 9), wl));
                if (PT_GT(cs, zero) && PT_GT(cl, zero)) {
                    uint32_t blocker;
                    PT_FN(intersect)(sc, l, PT_FN(vadd)(h, PT_FN(vscale)(ns, PT_K(PT_C_EPS))), wl,
                                     PT_SUB(dist, PT_K(PT_C_SHADOW_EPS)), &blocker);
                    if (blocker == 0) {
                        pt_v le = PT_FN(ldv)(sc, l->mat + PT_MAT_WORDS * (size_t)sc[lb + 15] + 4);
                        float g = PT_DIV(PT_MUL(PT_MUL(PT_MUL(cs, cl), PT_FN(ldr)(sc, lb + 14)), PT_K(PT_C_INV_PI)), dist2);
                        acc = PT_FN(vadd)(acc, PT_FN(vscale)(PT_FN(vmul)(PT_FN(vmul)(tp, albedo), le), g));
                    }
                }
            }
            /* A cosine-weighted bounce: the albedo is the whole weight. */
            float u1 = PT_FN(u01)(pt_rnd(key, dim + 2));
            float r = PT_FN(sqrt)(u1);
            float c, sn;
            PT_FN(cis)(pt_rnd(key, dim + 3), &c, &sn);
            d = PT_FN(normalize)(PT_FN(onb)(ns, PT_MUL(r, c), PT_MUL(r, sn), PT_FN(sqrt)(PT_SUB(one, u1))));
            o = PT_FN(vadd)(h, PT_FN(vscale)(ns, PT_K(PT_C_EPS)));
            spec = 0;
        } else if (kind == PT_METAL) {
            float z = PT_SUB(one, PT_MUL(PT_K(PT_C_TWO), PT_FN(u01)(pt_rnd(key, dim + 2))));
            float r = PT_FN(sqrt)(PT_SUB(one, PT_MUL(z, z)));
            float c, sn;
            PT_FN(cis)(pt_rnd(key, dim + 3), &c, &sn);
            float fuzz = PT_FN(ldr)(sc, mb + 7);
            d = PT_FN(normalize)(PT_FN(vadd)(PT_FN(reflect)(d, ns), PT_FN(vscale)(PT_FN(v3)(PT_MUL(r, c), PT_MUL(r, sn), z), fuzz)));
            if (!PT_GT(PT_FN(dot)(d, ns), zero)) break;
            o = PT_FN(vadd)(h, PT_FN(vscale)(ns, PT_K(PT_C_EPS)));
            spec = 1;
        } else {
            float ior = PT_FN(ldr)(sc, mb + 7);
            float ci = PT_NEG(PT_FN(dot)(d, ns));
            float eta = front ? PT_DIV(one, ior) : ior;
            float sin2 = PT_MUL(PT_MUL(eta, eta), PT_SUB(one, PT_MUL(ci, ci)));
            float r0 = PT_DIV(PT_SUB(one, ior), PT_ADD(one, ior));
            r0 = PT_MUL(r0, r0);
            float m = PT_SUB(one, ci);
            float fr = PT_ADD(r0, PT_MUL(PT_SUB(one, r0), PT_MUL(PT_MUL(PT_MUL(PT_MUL(m, m), m), m), m)));
            if (PT_GT(sin2, one) || PT_LT(PT_FN(u01)(pt_rnd(key, dim + 4)), fr)) {
                d = PT_FN(reflect)(d, ns);
                o = PT_FN(vadd)(h, PT_FN(vscale)(ns, PT_K(PT_C_EPS)));
            } else {
                float ct = PT_FN(sqrt)(PT_SUB(one, sin2));
                d = PT_FN(normalize)(PT_FN(vadd)(PT_FN(vscale)(d, eta), PT_FN(vscale)(ns, PT_SUB(PT_MUL(eta, ci), ct))));
                o = PT_FN(vsub)(h, PT_FN(vscale)(ns, PT_K(PT_C_EPS)));
            }
            spec = 1;
        }
        tp = PT_FN(vmul)(tp, albedo);
        if (b >= PT_RR_DEPTH) {
            float p = tp.x;
            if (PT_GT(tp.y, p)) p = tp.y;
            if (PT_GT(tp.z, p)) p = tp.z;
            if (PT_LT(p, PT_K(PT_C_RR_LO))) p = PT_K(PT_C_RR_LO);
            if (PT_GT(p, PT_K(PT_C_RR_HI))) p = PT_K(PT_C_RR_HI);
            if (!PT_LT(PT_FN(u01)(pt_rnd(key, dim + 5)), p)) break;
            tp = PT_FN(v3)(PT_DIV(tp.x, p), PT_DIV(tp.y, p), PT_DIV(tp.z, p));
        }
    }
    float cl = PT_FN(ldr)(sc, PT_H_CLAMP);
    return PT_FN(v3)(PT_FN(clamp_sample)(acc.x, cl), PT_FN(clamp_sample)(acc.y, cl), PT_FN(clamp_sample)(acc.z, cl));
}

/* Pass p of pixel (x, y): its samples p S .. p S + S - 1 summed in order from +0. */
PT_API pt_v PT_FN(pixel_pass)(const uint32_t *sc, const pt_layout *l, uint32_t x, uint32_t y, uint32_t p) {
    uint32_t spp = sc[PT_H_SPP];
    pt_v sum = PT_FN(zero3)();
    for (uint32_t s = p * spp; s < (p + 1) * spp; s++) sum = PT_FN(vadd)(sum, PT_FN(sample)(sc, l, x, y, s));
    return sum;
}
