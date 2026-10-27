/* The gemm native program's inputs and reference (mesh-ref lib.rs `gemm`). */
#include "mesh/ref/gemm.h"

#include <math.h>
#include <stdlib.h>

float mesh_gemm_a(size_t i, size_t k) { return (float)((i * 7 + k * 13) % 17) / 8.0f - 1.0f; }
float mesh_gemm_b(size_t k, size_t j) { return (float)((k * 5 + j * 11) % 19) / 9.0f - 1.0f; }

void mesh_gemm_inputs(size_t n, float *a, float *b) {
    for (size_t x = 0; x < n * n; x++) {
        a[x] = mesh_gemm_a(x / n, x % n);
        b[x] = mesh_gemm_b(x / n, x % n);
    }
}

void mesh_gemm_reference(size_t n, double *c, double *s) {
    for (size_t x = 0; x < n * n; x++) c[x] = s[x] = 0.0;
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++)
            for (size_t k = 0; k < n; k++) {
                double p = (double)mesh_gemm_a(i, k) * (double)mesh_gemm_b(k, j);
                c[i * n + j] += p;
                s[i * n + j] += fabs(p);
            }
}

double mesh_gemm_max_rel_err(size_t n, const float *got) {
    double *c = malloc(n * n * sizeof *c), *s = malloc(n * n * sizeof *s);
    if (!c || !s) {
        free(c), free(s);
        return nan("");
    }
    mesh_gemm_reference(n, c, s);
    double worst = 0.0;
    for (size_t x = 0; x < n * n; x++) {
        double e = fabs((double)got[x] - c[x]) / (s[x] + 1e-30);
        worst = fmax(worst, e); /* f64::max: a NaN error is ignored, as in the Rust fold */
    }
    free(c), free(s);
    return worst;
}
