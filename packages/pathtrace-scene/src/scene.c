/* The scene format's layout, validation and settable words (pathtrace-core lib.rs). */
#include "pathtrace/scene.h"

const size_t pt_settable[PT_SETTABLE_COUNT] = {PT_H_WIDTH, PT_H_HEIGHT, PT_H_SPP,
                                               PT_H_PASSES, PT_H_DEPTH, PT_H_SEED};

pt_layout pt_layout_of(const uint32_t *sc) {
    pt_layout l;
    uint32_t nmat = sc[PT_H_NMAT];
    l.nquad = sc[PT_H_NQUAD];
    l.nsph = sc[PT_H_NSPH];
    l.ntri = sc[PT_H_NTRI];
    l.mat = PT_HEADER_WORDS;
    l.quad = l.mat + PT_MAT_WORDS * (size_t)nmat;
    l.sph = l.quad + PT_QUAD_WORDS * (size_t)l.nquad;
    l.tri = l.sph + PT_SPH_WORDS * (size_t)l.nsph;
    l.light = sc[PT_H_LIGHT];
    return l;
}

uint32_t pt_validate(const uint32_t *sc, size_t len) { return pt_validate_in(sc, len, PT_MAX_TILES); }

uint32_t pt_validate_in(const uint32_t *sc, size_t len, uint32_t max_tiles) {
    if (len < PT_HEADER_WORDS) return 1;
    if (sc[PT_H_MAGIC] != PT_MAGIC_SCENE || sc[PT_H_VERSION] != PT_VERSION) return 2;
    size_t words = sc[PT_H_WORDS];
    if (words > len || words > PT_SCENE_MAX_WORDS) return 3;
    uint32_t w = sc[PT_H_WIDTH], h = sc[PT_H_HEIGHT];
    if (w == 0 || h == 0 || w % PT_TILE_W != 0 || h % PT_TILE_H != 0 ||
        (uint64_t)(w / PT_TILE_W) * (h / PT_TILE_H) > max_tiles)
        return 4;
    if (sc[PT_H_SPP] < 1 || sc[PT_H_SPP] > 1024 || sc[PT_H_PASSES] < 1 || sc[PT_H_PASSES] > 4096 ||
        sc[PT_H_DEPTH] < 1 || sc[PT_H_DEPTH] > 16)
        return 5;
    uint32_t nmat = sc[PT_H_NMAT], nquad = sc[PT_H_NQUAD], nsph = sc[PT_H_NSPH], ntri = sc[PT_H_NTRI];
    if (nmat == 0 || nmat > 64 || nquad > 64 || nsph > 64 || ntri > 64) return 6;
    pt_layout l = pt_layout_of(sc);
    if (l.tri + PT_TRI_WORDS * (size_t)ntri != words) return 7;
    for (size_t i = 0; i < nmat; i++)
        if (sc[l.mat + PT_MAT_WORDS * i] > PT_EMIT) return 8;
    for (size_t i = 0; i < nquad; i++)
        if (sc[l.quad + PT_QUAD_WORDS * i + 15] >= nmat) return 9;
    for (size_t i = 0; i < nsph; i++)
        if (sc[l.sph + PT_SPH_WORDS * i + 5] >= nmat) return 9;
    for (size_t i = 0; i < ntri; i++)
        if (sc[l.tri + PT_TRI_WORDS * i + 12] >= nmat) return 9;
    if (sc[PT_H_LIGHT] != PT_NO_LIGHT && sc[PT_H_LIGHT] >= nquad) return 10;
    return 0;
}

bool pt_set_param(uint32_t *sc, size_t len, uint32_t arg) {
    size_t word = arg >> 24;
    uint32_t value = arg & 0x00ffffffu;
    bool settable = false;
    for (int i = 0; i < PT_SETTABLE_COUNT; i++) settable |= pt_settable[i] == word;
    if (!settable || word >= len) return false;
    sc[word] = value;
    return true;
}
