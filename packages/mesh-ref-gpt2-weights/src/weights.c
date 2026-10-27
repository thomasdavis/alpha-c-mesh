/* Synthetic GPT-2 weights (mesh-ref gpt2.rs `synthetic`). */
#include "mesh/ref/gpt2_weights.h"

#include "mesh/ftz.h"
#include "mesh/ref/gpt2_shape.h"
#include "mesh/ref/matmul.h"

#include <stdlib.h>
#include <string.h>

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

float *mesh_gpt2_synthetic(uint32_t seed) {
    mesh_gpt2_layout lay;
    mesh_gpt2_layout_init(&lay);
    float *w = malloc(lay.total * sizeof *w);
    if (!w) return NULL;
    for (size_t i = 0; i < MESH_GPT2_TENSORS; i++) {
        char name[32];
        size_t off, n;
        mesh_gpt2_tensor(&lay, i, name, &off, &n);
        uint32_t s = seed * 0x9e3779b9u + (uint32_t)i;
        int ln = strstr(name, "ln_") != NULL, bias = ends_with(name, ".bias");
        float scale = ln ? 0.1f : bias ? 0.01f : 0.05f;
        float add = ln && !bias ? 1.0f : 0.0f;
        for (size_t x = 0; x < n; x++)
            w[off + x] = mesh_ftz_add(mesh_ftz_mul(mesh_matmul_value(s, (uint32_t)x), scale), add);
    }
    return w;
}
