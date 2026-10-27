/* programs/train's and infer's CPU reference (mesh-ref lib.rs `cubic`, `warp_sum`, `step_sums`,
 * `apply`, `loss`, `sin_pi`, `grid`, `samples`). */
#include "mesh/ref/cubic.h"

#include "mesh/abi/view_train.h"
#include "mesh/ftz.h"
#include "mesh/ref/block.h"

float mesh_cubic(const float p[4], float x) {
    float x2 = mesh_ftz_mul(x, x), x3 = mesh_ftz_mul(x2, x);
    return mesh_ftz_add(mesh_ftz_add(mesh_ftz_add(p[0], mesh_ftz_mul(p[1], x)), mesh_ftz_mul(p[2], x2)),
                        mesh_ftz_mul(p[3], x3));
}

float mesh_cubic_warp_sum(const float v[32]) {
    float w[32];
    for (int l = 0; l < 32; l++) w[l] = v[l];
    mesh_block_warp_reduce_f32(w, mesh_block_add_f32);
    return w[0];
}

void mesh_cubic_step_sums(const float p[4], const float *xs, const float *ys, uint32_t off, float out[5]) {
    float lanes[5][32];
    for (uint32_t l = 0; l < 32; l++) {
        size_t i = (8 * l + off) % 256;
        float x = xs[i], y = ys[i];
        float e = mesh_ftz_sub(mesh_cubic(p, x), y);
        float g = mesh_ftz_mul(2.0f, e);
        float x2 = mesh_ftz_mul(x, x);
        lanes[0][l] = mesh_ftz_mul(e, e);
        lanes[1][l] = g;
        lanes[2][l] = mesh_ftz_mul(g, x);
        lanes[3][l] = mesh_ftz_mul(g, x2);
        lanes[4][l] = mesh_ftz_mul(g, mesh_ftz_mul(x2, x));
    }
    for (int k = 0; k < 5; k++) out[k] = mesh_cubic_warp_sum(lanes[k]);
}

void mesh_cubic_apply(float p[4], const float s[5], float lr, float inv) {
    for (int k = 0; k < 4; k++) p[k] = mesh_ftz_sub(p[k], mesh_ftz_mul(mesh_ftz_mul(lr, s[k + 1]), inv));
}

float mesh_cubic_loss(const float s[5], float inv) { return mesh_ftz_mul(s[0], inv); }

float mesh_cubic_sin_pi(float x) {
    double t = mesh_ftz_to_f64(x);
    if (t > 0.5)
        t = 1.0 - t;
    else if (t < -0.5)
        t = -1.0 - t;
    double a = 3.141592653589793 * t;
    double a2 = a * a;
    double s = 1.0;
    for (uint32_t k = 11; k >= 1; k--) s = 1.0 - a2 / (double)((2 * k) * (2 * k + 1)) * s;
    return mesh_ftz_from_f64(a * s);
}

void mesh_cubic_grid(uint32_t n, float *out) {
    float d = mesh_ftz_from_u32(n - 1);
    for (uint32_t i = 0; i < n; i++) out[i] = mesh_ftz_add(-1.0f, mesh_ftz_div(mesh_ftz_mul(2.0f, mesh_ftz_from_u32(i)), d));
}

void mesh_cubic_samples(float *xs, float *ys) {
    mesh_cubic_grid(MESH_TRAIN_SAMPLES, xs);
    for (uint32_t i = 0; i < MESH_TRAIN_SAMPLES; i++) ys[i] = mesh_cubic_sin_pi(xs[i]);
}
