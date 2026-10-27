/* Golden digests from the Rust (pathtrace-core, R = mesh-ref's Z): four tiles of the default
 * scene over two passes (pass digests, fold digests, pixels, accumulators, a re-tone), the scalar
 * functions (sqrt, cis, quantize, tonemap) over 200 000 inputs, and 4000 camera samples. */
#include "pathtrace/z.h"

#include "pathtrace/default_scene.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

static uint64_t fh(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

/* The operand generator of the golden program (mesh-ftz's tests have the same). */
static uint32_t gen(uint32_t m, uint32_t i) {
    uint32_t h = mix((i * 0x9e3779b9u) ^ (m * 0x85ebca6bu));
    uint32_t h2 = mix(h ^ 0x27d4eb2fu);
    if (m == 0) return h;
    if (m == 1) return (h & 0x80000000u) | ((h2 % 41) << 23) | (h & 0x007fffffu);
    return (h & 0x80000000u) | ((50 + h2 % 31) << 23) | (h & 0x007fffffu);
}

static void want(const char *what, uint64_t got, uint64_t want) {
    if (got != want) {
        fprintf(stderr, "%s: %#018llx, want %#018llx\n", what, (unsigned long long)got, (unsigned long long)want);
        failures++;
    }
}

static void tiles(const uint32_t *sc) {
    uint64_t h = 0xcbf29ce484222325ull;
    const uint32_t ts[4] = {0, 42, 157, 299};
    pt_v *acc = malloc(PT_TILE_PIX * sizeof *acc), *part = malloc(PT_TILE_PIX * sizeof *part);
    uint32_t *rgb = malloc(PT_TILE_PIX * sizeof *rgb);
    for (int i = 0; i < 4; i++) {
        for (uint32_t p = 0; p < PT_TILE_PIX; p++) acc[p] = (pt_v){0, 0, 0}, rgb[p] = 0;
        for (uint32_t p = 0; p < 2; p++) {
            h = fh(h, pt_tile_pass_z(sc, ts[i], p, part));
            uint64_t da, dr;
            pt_tile_fold_z(sc, p + 1, p % 2, acc, part, rgb, &da, &dr);
            h = fh(fh(h, da), dr);
        }
        for (uint32_t p = 0; p < PT_TILE_PIX; p++) h = fh(h, rgb[p]);
        for (uint32_t p = 0; p < PT_TILE_PIX; p++)
            h = fh(fh(fh(h, pt_to_bits(acc[p].x)), pt_to_bits(acc[p].y)), pt_to_bits(acc[p].z));
        h = fh(h, pt_tile_retone_z(sc, 2, 1, acc, rgb));
    }
    free(acc), free(part), free(rgb);
    want("tiles", h, 0x0ebdc45fd60c40a7ull);
}

static void scalars(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 200000; i++) {
        float x = pt_from_bits(gen(i % 3, i));
        h = fh(h, pt_to_bits(pt_sqrt_z(x)));
        float c, s;
        pt_cis_z(mix(i), &c, &s);
        h = fh(fh(h, pt_to_bits(c)), pt_to_bits(s));
        h = fh(h, pt_quantize_z(x));
        h = fh(h, pt_quantize_z(pt_from_bits(mix(i) >> 2)));
        pt_v v = {x, pt_from_bits(mix(i) >> 1), pt_from_bits(0x3f000000u ^ (i & 0xffff))};
        h = fh(h, pt_tonemap_z(v, 1 + i % 64, pt_from_bits(0x3f800000u), i % 2));
    }
    want("scalars", h, 0x2b90b937379bba2aull);
}

static void samples(const uint32_t *sc) {
    pt_layout l = pt_layout_of(sc);
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 4000; i++) {
        pt_v v = pt_sample_z(sc, &l, mix(i) % 640, mix(i ^ 0xabcdefu) % 360, i);
        h = fh(fh(fh(h, pt_to_bits(v.x)), pt_to_bits(v.y)), pt_to_bits(v.z));
    }
    want("samples", h, 0x15444c3b8761be83ull);
}

int main(void) {
    tiles(pt_default_scene); /* the same digest as the f32 instantiation's: no value here is subnormal */
    scalars();
    samples(pt_default_scene);
    return failures != 0;
}
