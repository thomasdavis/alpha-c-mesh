/* Declares the public functions of one instantiation of the renderer (pathtrace/instantiate.h),
 * named pt_<name><PT_SUFFIX>: what an instantiation package's header includes with its PT_SUFFIX. */
#include "pathtrace/real.h"
#include "pathtrace/scene.h"

float PT_FN(sqrt)(float x);
void PT_FN(cis)(uint32_t h, float *cos_out, float *sin_out);
float PT_FN(intersect)(const uint32_t *sc, const pt_layout *l, pt_v o, pt_v d, float tmax, uint32_t *id);
pt_v PT_FN(sample)(const uint32_t *sc, const pt_layout *l, uint32_t x, uint32_t y, uint32_t s);
pt_v PT_FN(pixel_pass)(const uint32_t *sc, const pt_layout *l, uint32_t x, uint32_t y, uint32_t p);
uint32_t PT_FN(quantize)(float x);
uint32_t PT_FN(tonemap)(pt_v acc, uint32_t n, float exposure, uint32_t op);
uint64_t PT_FN(col_digest)(const pt_v c[PT_TILE_H]);
uint64_t PT_FN(column_pass)(const uint32_t *sc, const pt_layout *l, uint32_t t, uint32_t cx, uint32_t p,
                            pt_v part[PT_TILE_H]);
void PT_FN(retone)(const uint32_t *sc, uint32_t passes, uint32_t op, const pt_v acc[PT_TILE_H],
                   uint32_t rgb[PT_TILE_H], uint64_t *acc_digest, uint64_t *rgb_digest);
void PT_FN(fold_column)(const uint32_t *sc, uint32_t passes, uint32_t op, pt_v acc[PT_TILE_H],
                        const pt_v part[PT_TILE_H], uint32_t rgb[PT_TILE_H], uint64_t *acc_digest,
                        uint64_t *rgb_digest);
uint64_t PT_FN(tile_pass)(const uint32_t *sc, uint32_t t, uint32_t p, pt_v *part);
void PT_FN(tile_fold)(const uint32_t *sc, uint32_t passes, uint32_t op, pt_v *acc, const pt_v *part, uint32_t *rgb,
                      uint64_t *acc_digest, uint64_t *rgb_digest);
uint64_t PT_FN(tile_retone)(const uint32_t *sc, uint32_t passes, uint32_t op, const pt_v *acc, uint32_t *rgb);
