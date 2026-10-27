/* GPT-2's KV cache (mesh-ref gpt2.rs `Kv`). */
#include "mesh/ref/gpt2_kv.h"

#include "mesh/ref/gpt2_logits.h"

#include <stdlib.h>

void mesh_gpt2_kv_init(mesh_gpt2_kv *kv) {
    for (int l = 0; l < MESH_GPT2_LAYERS; l++) kv->k[l] = kv->v[l] = NULL, kv->rows[l] = 0;
    kv->cap = 0;
}

void mesh_gpt2_kv_free(mesh_gpt2_kv *kv) {
    for (int l = 0; l < MESH_GPT2_LAYERS; l++) free(kv->k[l]), free(kv->v[l]);
    mesh_gpt2_kv_init(kv);
}

bool mesh_gpt2_kv_reserve(mesh_gpt2_kv *kv, size_t positions) {
    if (positions <= kv->cap) return true;
    size_t cap = kv->cap ? kv->cap : 16;
    while (cap < positions) cap *= 2;
    for (int l = 0; l < MESH_GPT2_LAYERS; l++) {
        float *k = realloc(kv->k[l], cap * MESH_GPT2_WIDTH * sizeof *k);
        if (!k) return false;
        kv->k[l] = k;
        float *v = realloc(kv->v[l], cap * MESH_GPT2_WIDTH * sizeof *v);
        if (!v) return false;
        kv->v[l] = v;
    }
    kv->cap = cap;
    return true;
}

uint64_t mesh_gpt2_kv_digest(const mesh_gpt2_kv *kv) {
    uint64_t h = MESH_GPT2_DIGEST_SEED;
    for (int l = 0; l < MESH_GPT2_LAYERS; l++) {
        h = mesh_gpt2_digest_extend(h, kv->k[l], kv->rows[l] * MESH_GPT2_WIDTH);
        h = mesh_gpt2_digest_extend(h, kv->v[l], kv->rows[l] * MESH_GPT2_WIDTH);
    }
    return h;
}
