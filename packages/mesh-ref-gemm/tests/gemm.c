/* A golden digest from the Rust over the inputs, the reference and the error at n = 1, 7, 64. */
#include "mesh/ref/gemm.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t fnv(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static uint64_t bits64(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

int main(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    const size_t ns[3] = {1, 7, 64};
    for (int t = 0; t < 3; t++) {
        size_t n = ns[t];
        float *a = malloc(n * n * 4), *b = malloc(n * n * 4), *got = malloc(n * n * 4);
        double *c = malloc(n * n * 8), *s = malloc(n * n * 8);
        mesh_gemm_inputs(n, a, b);
        for (size_t i = 0; i < n * n; i++) {
            uint32_t u;
            memcpy(&u, &a[i], 4);
            h = fnv(h, u);
        }
        for (size_t i = 0; i < n * n; i++) {
            uint32_t u;
            memcpy(&u, &b[i], 4);
            h = fnv(h, u);
        }
        mesh_gemm_reference(n, c, s);
        for (size_t i = 0; i < n * n; i++) h = fnv(h, bits64(c[i]));
        for (size_t i = 0; i < n * n; i++) h = fnv(h, bits64(s[i]));
        for (size_t i = 0; i < n * n; i++) got[i] = (float)c[i];
        h = fnv(h, bits64(mesh_gemm_max_rel_err(n, got)));
        free(a), free(b), free(got), free(c), free(s);
    }
    if (h != 0xfb5560602c4daaf4ull) {
        fprintf(stderr, "gemm digest %#018llx, want 0xfb5560602c4daaf4\n", (unsigned long long)h);
        return 1;
    }
    return 0;
}
