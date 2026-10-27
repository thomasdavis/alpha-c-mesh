/* programs/pathtrace's digests (pathtrace-core lib.rs `fold`, `payload_digest`; mesh-ref `fnv_words`). */
#include "pathtrace/digest.h"

#include "pathtrace/scene.h"

uint64_t pt_fold(const uint64_t cols[32]) {
    uint64_t h = PT_FNV_OFFSET;
    for (int c = 0; c < 32; c++) h = pt_fnv(pt_fnv(h, (uint32_t)cols[c]), (uint32_t)(cols[c] >> 32));
    return h;
}

uint64_t pt_fnv_words(const uint32_t *w, size_t n) {
    uint64_t h = PT_FNV_OFFSET;
    for (size_t i = 0; i < n; i++) h = pt_fnv(h, w[i]);
    return h;
}

uint64_t pt_payload_digest(const uint32_t *words) {
    uint64_t cols[32];
    for (uint32_t cx = 0; cx < 32; cx++) {
        uint64_t h = PT_FNV_OFFSET;
        for (uint32_t row = 0; row < PT_TILE_H; row++) {
            size_t o = 3 * (size_t)(row * PT_TILE_W + cx);
            h = pt_fnv(pt_fnv(pt_fnv(h, words[o]), words[o + 1]), words[o + 2]);
        }
        cols[cx] = h;
    }
    return pt_fold(cols);
}
