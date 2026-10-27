/* Template (included once per instantiation after pathtrace/t_math.h; see pathtrace/real.h): the
 * nearest hit of a ray in (EPS, tmax) against the scene's quads, spheres and triangles (the triangles
 * behind their bounding sphere). Returns t, and the hit id in *id (0 none, 1 + i quad i, PT_ID_SPH + i
 * sphere i, PT_ID_TRI + i triangle i). */
#include "pathtrace/real.h"
#include "pathtrace/scene.h"

PT_API float PT_FN(intersect)(const uint32_t *sc, const pt_layout *l, pt_v o, pt_v d, float tmax, uint32_t *id_out) {
    float tmin = PT_K(PT_C_EPS), one = PT_K(PT_C_ONE), zero = PT_K(PT_C_ZERO);
    float best = tmax;
    uint32_t id = 0;
    for (uint32_t i = 0; i < l->nquad; i++) {
        size_t b = l->quad + PT_QUAD_WORDS * (size_t)i;
        pt_v n = PT_FN(ldv)(sc, b + 9);
        float den = PT_FN(dot)(n, d);
        if (PT_GT(den, PT_NEG(PT_K(PT_C_PAR_EPS))) && PT_LT(den, PT_K(PT_C_PAR_EPS))) continue;
        pt_v p = PT_FN(ldv)(sc, b);
        float t = PT_DIV(PT_FN(dot)(n, PT_FN(vsub)(p, o)), den);
        if (!(PT_GT(t, tmin) && PT_LT(t, best))) continue;
        pt_v w = PT_FN(vsub)(PT_FN(vadd)(o, PT_FN(vscale)(d, t)), p);
        float a = PT_MUL(PT_FN(dot)(w, PT_FN(ldv)(sc, b + 3)), PT_FN(ldr)(sc, b + 12));
        if (PT_LT(a, zero) || PT_GT(a, one)) continue;
        float c = PT_MUL(PT_FN(dot)(w, PT_FN(ldv)(sc, b + 6)), PT_FN(ldr)(sc, b + 13));
        if (PT_LT(c, zero) || PT_GT(c, one)) continue;
        best = t;
        id = 1 + i;
    }
    for (uint32_t i = 0; i < l->nsph; i++) {
        size_t b = l->sph + PT_SPH_WORDS * (size_t)i;
        float r = PT_FN(ldr)(sc, b + 3);
        pt_v oc = PT_FN(vsub)(o, PT_FN(ldv)(sc, b));
        float bq = PT_FN(dot)(oc, d);
        float cq = PT_SUB(PT_FN(dot)(oc, oc), PT_MUL(r, r));
        float disc = PT_SUB(PT_MUL(bq, bq), cq);
        if (PT_LT(disc, zero)) continue;
        float s = PT_FN(sqrt)(disc);
        float t = PT_SUB(PT_NEG(bq), s);
        if (!PT_GT(t, tmin)) t = PT_SUB(s, bq);
        if (PT_GT(t, tmin) && PT_LT(t, best)) {
            best = t;
            id = PT_ID_SPH + i;
        }
    }
    if (l->ntri > 0) {
        /* The mesh's bounding sphere first. */
        float mr = PT_FN(ldr)(sc, PT_H_MESH + 3);
        pt_v oc = PT_FN(vsub)(o, PT_FN(ldv)(sc, PT_H_MESH));
        float bq = PT_FN(dot)(oc, d);
        float cq = PT_SUB(PT_FN(dot)(oc, oc), PT_MUL(mr, mr));
        float disc = PT_SUB(PT_MUL(bq, bq), cq);
        int near = !PT_LT(disc, zero);
        if (near) {
            float s = PT_FN(sqrt)(disc);
            near = PT_GT(PT_SUB(s, bq), tmin) && PT_LT(PT_SUB(PT_NEG(bq), s), best);
        }
        if (near) {
            for (uint32_t i = 0; i < l->ntri; i++) {
                size_t b = l->tri + PT_TRI_WORDS * (size_t)i;
                pt_v e1 = PT_FN(ldv)(sc, b + 3);
                pt_v e2 = PT_FN(ldv)(sc, b + 6);
                pt_v pv = PT_FN(cross)(d, e2);
                float det = PT_FN(dot)(e1, pv);
                if (PT_GT(det, PT_NEG(PT_K(PT_C_DET_EPS))) && PT_LT(det, PT_K(PT_C_DET_EPS))) continue;
                float inv = PT_DIV(one, det);
                pt_v tv = PT_FN(vsub)(o, PT_FN(ldv)(sc, b));
                float u = PT_MUL(PT_FN(dot)(tv, pv), inv);
                if (PT_LT(u, zero) || PT_GT(u, one)) continue;
                pt_v qv = PT_FN(cross)(tv, e1);
                float v = PT_MUL(PT_FN(dot)(d, qv), inv);
                if (PT_LT(v, zero) || PT_GT(PT_ADD(u, v), one)) continue;
                float t = PT_MUL(PT_FN(dot)(e2, qv), inv);
                if (PT_GT(t, tmin) && PT_LT(t, best)) {
                    best = t;
                    id = PT_ID_TRI + i;
                }
            }
        }
    }
    *id_out = id;
    return best;
}
