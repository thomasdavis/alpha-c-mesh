/* Template (included once per instantiation after t_math, t_intersect, t_sample and t_tonemap; see
 * pathtrace/real.h): a tile's columns (one column a lane of a warp, or 32 columns in a loop on one
 * CPU or wasm thread) and whole tiles. A column is PT_TILE_H pixels, top to bottom; a tile's pixels
 * are PT_TILE_PIX, row-major. Digests are FNV-1a 64 (@ajax/pathtrace-digest). */
#include "pathtrace/digest.h"
#include "pathtrace/real.h"
#include "pathtrace/scene.h"

/* FNV-1a 64 over a column's r g b words, rows top to bottom. */
PT_API uint64_t PT_FN(col_digest)(const pt_v c[PT_TILE_H]) {
    uint64_t h = PT_FNV_OFFSET;
    for (uint32_t r = 0; r < PT_TILE_H; r++) h = pt_fnv(pt_fnv(pt_fnv(h, pt_to_bits(c[r].x)), pt_to_bits(c[r].y)), pt_to_bits(c[r].z));
    return h;
}

/* Pass p of column cx of tile t: each pixel's pass sum (a work item's payload) into part; its digest. */
PT_API uint64_t PT_FN(column_pass)(const uint32_t *sc, const pt_layout *l, uint32_t t, uint32_t cx, uint32_t p,
                                   pt_v part[PT_TILE_H]) {
    uint32_t x0, y0;
    pt_tile_origin(sc, t, &x0, &y0);
    for (uint32_t row = 0; row < PT_TILE_H; row++) part[row] = PT_FN(pixel_pass)(sc, l, x0 + cx, y0 + row, p);
    return PT_FN(col_digest)(part);
}

/* Tone maps a column of accumulators (n = passes x S samples) into rgb; (acc digest, rgb digest). */
PT_API void PT_FN(retone)(const uint32_t *sc, uint32_t passes, uint32_t op, const pt_v acc[PT_TILE_H],
                          uint32_t rgb[PT_TILE_H], uint64_t *acc_digest, uint64_t *rgb_digest) {
    uint32_t n = passes * sc[PT_H_SPP];
    float exposure = PT_FN(ldr)(sc, PT_H_EXPOSURE);
    uint64_t hr = PT_FNV_OFFSET;
    for (uint32_t row = 0; row < PT_TILE_H; row++) {
        rgb[row] = PT_FN(tonemap)(acc[row], n, exposure, op);
        hr = pt_fnv(hr, rgb[row]);
    }
    *acc_digest = PT_FN(col_digest)(acc);
    *rgb_digest = hr;
}

/* One pass folded into a column's accumulators (acc + part; the caller keeps pass order), then the
 * tone map with n = passes S samples: (acc digest, rgb digest). */
PT_API void PT_FN(fold_column)(const uint32_t *sc, uint32_t passes, uint32_t op, pt_v acc[PT_TILE_H],
                               const pt_v part[PT_TILE_H], uint32_t rgb[PT_TILE_H], uint64_t *acc_digest,
                               uint64_t *rgb_digest) {
    for (uint32_t row = 0; row < PT_TILE_H; row++) acc[row] = PT_FN(vadd)(acc[row], part[row]);
    PT_FN(retone)(sc, passes, op, acc, rgb, acc_digest, rgb_digest);
}

static inline void PT_FN(col_of)(const pt_v *px, uint32_t cx, pt_v c[PT_TILE_H]) {
    for (uint32_t row = 0; row < PT_TILE_H; row++) c[row] = px[row * PT_TILE_W + cx];
}
static inline void PT_FN(put_col)(pt_v *px, uint32_t cx, const pt_v c[PT_TILE_H]) {
    for (uint32_t row = 0; row < PT_TILE_H; row++) px[row * PT_TILE_W + cx] = c[row];
}

/* Pass p of tile t into part (PT_TILE_PIX pixels): the tile's pass-sum digest. */
PT_API uint64_t PT_FN(tile_pass)(const uint32_t *sc, uint32_t t, uint32_t p, pt_v *part) {
    pt_layout l = pt_layout_of(sc);
    uint64_t d[32];
    for (uint32_t cx = 0; cx < PT_TILE_W; cx++) {
        pt_v c[PT_TILE_H];
        d[cx] = PT_FN(column_pass)(sc, &l, t, cx, p, c);
        PT_FN(put_col)(part, cx, c);
    }
    return pt_fold(d);
}

/* acc += part over the tile, tone mapped with n = passes S into rgb: the tile digests (acc, rgb). */
PT_API void PT_FN(tile_fold)(const uint32_t *sc, uint32_t passes, uint32_t op, pt_v *acc, const pt_v *part,
                             uint32_t *rgb, uint64_t *acc_digest, uint64_t *rgb_digest) {
    uint64_t ca[32], cr[32];
    for (uint32_t cx = 0; cx < PT_TILE_W; cx++) {
        pt_v a[PT_TILE_H], pc[PT_TILE_H];
        uint32_t r[PT_TILE_H];
        PT_FN(col_of)(acc, cx, a);
        PT_FN(col_of)(part, cx, pc);
        PT_FN(fold_column)(sc, passes, op, a, pc, r, &ca[cx], &cr[cx]);
        PT_FN(put_col)(acc, cx, a);
        for (uint32_t row = 0; row < PT_TILE_H; row++) rgb[row * PT_TILE_W + cx] = r[row];
    }
    *acc_digest = pt_fold(ca);
    *rgb_digest = pt_fold(cr);
}

/* Re-tones a tile's accumulators into rgb: its rgb digest. */
PT_API uint64_t PT_FN(tile_retone)(const uint32_t *sc, uint32_t passes, uint32_t op, const pt_v *acc, uint32_t *rgb) {
    uint64_t cr[32];
    for (uint32_t cx = 0; cx < PT_TILE_W; cx++) {
        pt_v a[PT_TILE_H];
        uint32_t r[PT_TILE_H];
        uint64_t ignored;
        PT_FN(col_of)(acc, cx, a);
        PT_FN(retone)(sc, passes, op, a, r, &ignored, &cr[cx]);
        for (uint32_t row = 0; row < PT_TILE_H; row++) rgb[row * PT_TILE_W + cx] = r[row];
    }
    return pt_fold(cr);
}
