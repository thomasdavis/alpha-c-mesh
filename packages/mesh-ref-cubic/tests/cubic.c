/* A golden digest from the Rust (mesh-ref lib.rs): the samples, a 64-point grid, sin_pi over 200 000
 * inputs, 300 SGD steps (sums, loss, parameters, the cubic at a point) and a warp sum. */
#include "mesh/ref/cubic.h"

#include "mesh/abi/view_train.h"
#include "mesh/ftz.h"

#include <stdio.h>

static uint64_t fh(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static uint32_t mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

static uint32_t gen(uint32_t m, uint32_t i) {
    uint32_t h = mix((i * 0x9e3779b9u) ^ (m * 0x85ebca6bu));
    uint32_t h2 = mix(h ^ 0x27d4eb2fu);
    if (m == 0) return h;
    if (m == 1) return (h & 0x80000000u) | ((h2 % 41) << 23) | (h & 0x007fffffu);
    return (h & 0x80000000u) | ((50 + h2 % 31) << 23) | (h & 0x007fffffu);
}

int main(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    float xs[MESH_TRAIN_SAMPLES], ys[MESH_TRAIN_SAMPLES], g[64];
    mesh_cubic_samples(xs, ys);
    for (uint32_t i = 0; i < MESH_TRAIN_SAMPLES; i++) h = fh(h, mesh_ftz_bits(xs[i]));
    for (uint32_t i = 0; i < MESH_TRAIN_SAMPLES; i++) h = fh(h, mesh_ftz_bits(ys[i]));
    mesh_cubic_grid(64, g);
    for (int i = 0; i < 64; i++) h = fh(h, mesh_ftz_bits(g[i]));
    for (uint32_t i = 0; i < 100000; i++) {
        h = fh(h, mesh_ftz_bits(mesh_cubic_sin_pi(mesh_ftz_from_bits(gen(i % 3, i)))));
        h = fh(h, mesh_ftz_bits(mesh_cubic_sin_pi(((float)i - 50000.0f) / 20000.0f)));
    }
    float p[4] = {0.1f, -0.2f, 0.3f, 0.05f}, s[5];
    for (uint32_t step = 0; step < 300; step++) {
        mesh_cubic_step_sums(p, xs, ys, step, s);
        for (int k = 0; k < 5; k++) h = fh(h, mesh_ftz_bits(s[k]));
        h = fh(h, mesh_ftz_bits(mesh_cubic_loss(s, 1.0f / 32.0f)));
        mesh_cubic_apply(p, s, 0.005f, 1.0f / 32.0f);
        for (int k = 0; k < 4; k++) h = fh(h, mesh_ftz_bits(p[k]));
        h = fh(h, mesh_ftz_bits(mesh_cubic(p, mesh_ftz_from_bits(gen(step % 3, step)))));
    }
    float v[32];
    for (uint32_t l = 0; l < 32; l++) v[l] = mesh_ftz_from_bits(gen(1, l));
    h = fh(h, mesh_ftz_bits(mesh_cubic_warp_sum(v)));
    if (h != 0xc34d0ef23c7cd3f0ull) {
        fprintf(stderr, "cubic %#018llx, want 0xc34d0ef23c7cd3f0\n", (unsigned long long)h);
        return 1;
    }
    return 0;
}
