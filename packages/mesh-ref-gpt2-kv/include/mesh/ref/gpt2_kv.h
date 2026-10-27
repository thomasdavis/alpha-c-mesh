/* @ajax/mesh-ref-gpt2-kv: GPT-2's KV cache (mesh-ref gpt2.rs `Kv`): per layer, K and V rows of WIDTH
 * (heads concatenated) for positions 0..len. Rows are only ever appended. Host. */
#ifndef MESH_REF_GPT2_KV_H
#define MESH_REF_GPT2_KV_H

#include "mesh/ref/gpt2_shape.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float *k[MESH_GPT2_LAYERS], *v[MESH_GPT2_LAYERS]; /* rows x WIDTH each */
    size_t rows[MESH_GPT2_LAYERS];                   /* positions held per layer (K and V alike) */
    size_t cap;                                      /* positions allocated */
} mesh_gpt2_kv;

/* Positions in the cache: layer 0's, as the Rust's `Kv::len` (during a forward pass the later layers
 * catch up one by one). */
static inline size_t mesh_gpt2_kv_len(const mesh_gpt2_kv *kv) { return kv->rows[0]; }

void mesh_gpt2_kv_init(mesh_gpt2_kv *kv);
void mesh_gpt2_kv_free(mesh_gpt2_kv *kv);
/* Room for `positions` positions in every layer (never shrinks). False when memory runs out. */
bool mesh_gpt2_kv_reserve(mesh_gpt2_kv *kv, size_t positions);
/* FNV-1a 64 of every layer's K then V rows (from the digest seed). */
uint64_t mesh_gpt2_kv_digest(const mesh_gpt2_kv *kv);

#endif
