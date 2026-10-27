/* Golden digests from the Rust (pathtrace-core, R = f32): four tiles of the default
 * scene over two passes (pass digests, fold digests, pixels, accumulators, a re-tone), the scalar
 * functions (sqrt, cis, quantize, tonemap) over 200 000 inputs, and 4000 camera samples. */
#include "pathtrace/f32.h"

#include "pathtrace/digest.h"

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
            h = fh(h, pt_tile_pass_f32(sc, ts[i], p, part));
            uint64_t da, dr;
            pt_tile_fold_f32(sc, p + 1, p % 2, acc, part, rgb, &da, &dr);
            h = fh(fh(h, da), dr);
        }
        for (uint32_t p = 0; p < PT_TILE_PIX; p++) h = fh(h, rgb[p]);
        for (uint32_t p = 0; p < PT_TILE_PIX; p++)
            h = fh(fh(fh(h, pt_to_bits(acc[p].x)), pt_to_bits(acc[p].y)), pt_to_bits(acc[p].z));
        h = fh(h, pt_tile_retone_f32(sc, 2, 1, acc, rgb));
    }
    free(acc), free(part), free(rgb);
    want("tiles", h, 0x0ebdc45fd60c40a7ull);
}

static void scalars(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 200000; i++) {
        float x = pt_from_bits(gen(i % 3, i));
        h = fh(h, pt_to_bits(pt_sqrt_f32(x)));
        float c, s;
        pt_cis_f32(mix(i), &c, &s);
        h = fh(fh(h, pt_to_bits(c)), pt_to_bits(s));
        h = fh(h, pt_quantize_f32(x));
        h = fh(h, pt_quantize_f32(pt_from_bits(mix(i) >> 2)));
        pt_v v = {x, pt_from_bits(mix(i) >> 1), pt_from_bits(0x3f000000u ^ (i & 0xffff))};
        h = fh(h, pt_tonemap_f32(v, 1 + i % 64, pt_from_bits(0x3f800000u), i % 2));
    }
    want("scalars", h, 0xcd7759b2f49657b3ull);
}

static void samples(const uint32_t *sc) {
    pt_layout l = pt_layout_of(sc);
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint32_t i = 0; i < 4000; i++) {
        pt_v v = pt_sample_f32(sc, &l, mix(i) % 640, mix(i ^ 0xabcdefu) % 360, i);
        h = fh(fh(fh(h, pt_to_bits(v.x)), pt_to_bits(v.y)), pt_to_bits(v.z));
    }
    want("samples", h, 0x15444c3b8761be83ull);
}

/* pathtrace-core's sqrt_and_cis_are_close and quantize_is_srgb. The references are computed in
 * double here (Newton for sqrt, Taylor series for cos and sin), so the test needs no libm. */
static double dsqrt(double x) {
    double y = x > 1 ? x : 1;
    for (int i = 0; i < 100; i++) y = 0.5 * (y + x / y);
    return y;
}

static void dcis(double a, double *c, double *s) {
    const double tau = 6.283185307179586;
    while (a > 3.141592653589793) a -= tau;
    double t = a, cs = 1, sn = a, a2 = a * a;
    for (int k = 1; k < 30; k++) {
        t = -t * a2 / ((2 * k) * (2 * k + 1));
        sn += t;
    }
    t = 1;
    for (int k = 1; k < 30; k++) {
        t = -t * a2 / ((2 * k - 1) * (2 * k));
        cs += t;
    }
    *c = cs, *s = sn;
}

static double dabs(double x) { return x < 0 ? -x : x; }

static void sqrt_and_cis_are_close(void) {
    const float xs[7] = {1e-6f, 0.01f, 0.5f, 1.0f, 2.0f, 3.0f, 1e6f};
    for (int i = 0; i < 7; i++) {
        double r = dsqrt(xs[i]);
        if (dabs(pt_sqrt_f32(xs[i]) - (float)r) > (float)r * 2e-7) failures++;
    }
    const uint32_t hs[6] = {0, 1u << 29, 1u << 30, 3u << 30, 0x12345678u, 0xffffffffu};
    for (int i = 0; i < 6; i++) {
        double a = 2.0 * 3.141592653589793 * ((double)(hs[i] >> 6) / (double)(1ull << 26)), rc, rs;
        dcis(a, &rc, &rs);
        float c, s;
        pt_cis_f32(hs[i], &c, &s);
        if (dabs(c - rc) >= 1e-6 || dabs(s - rs) >= 1e-6) {
            fprintf(stderr, "cis %#x: %g %g vs %g %g\n", hs[i], c, s, rc, rs);
            failures++;
        }
    }
}

static void quantize_is_srgb(void) {
    if (pt_quantize_f32(0.0f) != 0 || pt_quantize_f32(1.0f) != 255 || pt_quantize_f32(0.2122f) != 127 ||
        pt_quantize_f32(pt_from_bits(0x7fc00000u)) != 0)
        failures++;
}

/* A payload's digest (from its words, as meshd's assembler checks it) is the item's pass digest. */
static void payload_digest(void) {
    pt_v *part = malloc(PT_TILE_PIX * sizeof *part);
    uint32_t *words = malloc(3 * PT_TILE_PIX * sizeof *words);
    uint64_t d = pt_tile_pass_f32(pt_default_scene, 42, 3, part);
    for (uint32_t p = 0; p < PT_TILE_PIX; p++) {
        words[3 * p] = pt_to_bits(part[p].x);
        words[3 * p + 1] = pt_to_bits(part[p].y);
        words[3 * p + 2] = pt_to_bits(part[p].z);
    }
    want("payload", d, 0xbb4c3b272139e454ull);
    want("payload digest", pt_payload_digest(words), d);
    free(part), free(words);
}

int main(void) {
    tiles(pt_default_scene);
    scalars();
    samples(pt_default_scene);
    sqrt_and_cis_are_close();
    quantize_is_srgb();
    payload_digest();
    return failures != 0;
}
