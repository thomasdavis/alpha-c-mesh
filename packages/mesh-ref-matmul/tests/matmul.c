/* mesh-ref matmul.rs's test, then a golden digest from the Rust: matmul and matvec at four shapes and
 * a thousand values. */
#include "mesh/ref/matmul.h"

#include "mesh/ftz.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t fnv(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static void rust_exact_and_ordered(void) {
    size_t m = 5, k = 37, n = 3;
    float a[5 * 37], b[37 * 3], c[15], c1[15], y[5];
    mesh_matmul_matrix(1, m * k, a);
    mesh_matmul_matrix(2, k * n, b);
    for (size_t i = 0; i < m * k; i++) CHECK(a[i] >= -1.0f && a[i] < 1.0f);
    mesh_matmul(a, b, m, k, n, 3, c);
    mesh_matmul(a, b, m, k, n, 1, c1);
    for (size_t i = 0; i < m * n; i++) CHECK(mesh_ftz_bits(c[i]) == mesh_ftz_bits(c1[i]));
    /* The sequential order, as plain f32 (no subnormals here: FTZ changes nothing). */
    float acc = 0.0f;
    for (size_t kk = 0; kk < k; kk++) acc += a[2 * k + kk] * b[kk * n + 1];
    CHECK(mesh_ftz_bits(c[2 * n + 1]) == mesh_ftz_bits(acc));
    mesh_matvec(a, b, m, k, y);
}

static void rust_golden(void) {
    const size_t shapes[4][3] = {{5, 37, 3}, {16, 64, 16}, {33, 100, 7}, {64, 64, 64}};
    uint64_t h = 0xcbf29ce484222325ull;
    for (int s = 0; s < 4; s++) {
        size_t m = shapes[s][0], k = shapes[s][1], n = shapes[s][2];
        float *a = malloc(m * k * sizeof *a), *b = malloc(k * n * sizeof *b), *c = malloc(m * n * sizeof *c),
              *y = malloc(m * sizeof *y);
        mesh_matmul_matrix(1, m * k, a);
        mesh_matmul_matrix(2, k * n, b);
        mesh_matmul(a, b, m, k, n, 3, c);
        h = fnv(h, mesh_matmul_digest(c, m * n));
        mesh_matvec(a, b, m, k, y);
        for (size_t i = 0; i < m; i++) h = fnv(h, mesh_ftz_bits(y[i]));
        free(a), free(b), free(c), free(y);
    }
    for (uint32_t i = 0; i < 1000; i++) h = fnv(h, mesh_ftz_bits(mesh_matmul_value(i * 7919, i)));
    if (h != 0x03c20f795c51b1ecull) {
        fprintf(stderr, "matmul digest %#018llx, want 0x03c20f795c51b1ec\n", (unsigned long long)h);
        failures++;
    }
}

int main(void) {
    rust_exact_and_ordered();
    rust_golden();
    return failures != 0;
}
