/* Golden data from the Rust (mesh-ref pathtrace.rs): a 64 x 48 image of the default scene, 2 spp,
 * 3 passes, both tone maps: every tile's digests and the PPM's bytes. */
#include "mesh/ref/pathtrace.h"

#include "pathtrace/digest.h"

#include <stdio.h>
#include <stdlib.h>

#include "pathtrace/default_scene.h"

int main(void) {
    static uint32_t small[552];
    for (int i = 0; i < 552; i++) small[i] = pt_default_scene[i];
    const uint32_t sets[4][2] = {{PT_H_WIDTH, 64}, {PT_H_HEIGHT, 48}, {PT_H_SPP, 2}, {PT_H_PASSES, 3}};
    for (int i = 0; i < 4; i++) pt_set_param(small, 552, pt_set_arg(sets[i][0], sets[i][1]));
    int failures = 0;
    if (pt_validate(small, 552) != 0) failures++;
    const uint64_t want_tiles[2] = {0xc15abb728fdfd66full, 0x07d063bfcf961fe3ull};
    const uint64_t want_ppm[2] = {0x3b1c7084e9ca9e10ull, 0x3bbdf238a8f583eeull};
    for (uint32_t op = 0; op < 2; op++) {
        uint32_t n = pt_tiles(small), ts[4];
        for (uint32_t t = 0; t < n; t++) ts[t] = t;
        mesh_pt_tile tl[4];
        if (n != 4 || !mesh_pt_tiles(small, ts, n, 3, op, tl)) return 1;
        uint32_t rgb[4 * PT_TILE_PIX];
        uint64_t h = PT_FNV_OFFSET;
        for (uint32_t t = 0; t < n; t++) {
            for (uint32_t i = 0; i < PT_TILE_PIX; i++) rgb[t * PT_TILE_PIX + i] = tl[t].rgb[i];
            h = (h ^ tl[t].acc_digest) * PT_FNV_PRIME;
            h = (h ^ tl[t].rgb_digest) * PT_FNV_PRIME;
            for (uint32_t p = 0; p < 3; p++) h = (h ^ tl[t].part_digests[p]) * PT_FNV_PRIME;
            h = (h ^ pt_fnv_words(tl[t].acc, 3 * PT_TILE_PIX)) * PT_FNV_PRIME;
            mesh_pt_tile_free(&tl[t]);
        }
        static uint8_t raster[64 * 48 * 3];
        mesh_pt_raster(small, rgb, raster);
        size_t len;
        uint8_t *ppm = mesh_pt_ppm(64, 48, raster, &len);
        uint64_t hp = PT_FNV_OFFSET;
        for (size_t i = 0; i < len; i++) hp = (hp ^ ppm[i]) * PT_FNV_PRIME;
        free(ppm);
        if (h != want_tiles[op] || len != 9229 || hp != want_ppm[op]) {
            fprintf(stderr, "op %u: tiles %#018llx ppm %zu bytes %#018llx\n", op, (unsigned long long)h, len,
                    (unsigned long long)hp);
            failures++;
        }
    }
    return failures != 0;
}
