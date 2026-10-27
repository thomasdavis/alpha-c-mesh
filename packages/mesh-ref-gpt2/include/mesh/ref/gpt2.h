/* @ajax/mesh-ref-gpt2: GPT-2 124M in the cluster float mode (mesh-ref gpt2.rs), the CPU reference of
 * programs/gpt2x (the exact-mode GPU kernels) and the exact executor of programs/gpt2 on a CPU. Every
 * value is a fixed function of the weights and the token prefix: the order of every sum is fixed and
 * followed op for op by the GPU kernels, so the logits and the KV cache are bitwise the same on every
 * executor, and a row computed alone (decode) equals the same row computed in a batch (prefill).
 * Work is split over POSIX threads; the results never depend on the thread count. Host. */
#ifndef MESH_REF_GPT2_H
#define MESH_REF_GPT2_H

#include "mesh/ref/gpt2_kv.h"
#include "mesh/ref/gpt2_shape.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The logits (VOCAB) of a normalized row h: wte times h in matmul's warp-strided GEMV order. */
void mesh_gpt2_project_vocab(const float *wte, const float *h, size_t threads, float *out);

/* Called once layer l's K and V are in the cache (a prefill streams them from there). */
typedef void (*mesh_gpt2_after_layer)(size_t layer, const mesh_gpt2_kv *kv, void *ctx);

/* The rows `tokens` at positions mesh_gpt2_kv_len(kv).. through the model; their K and V are appended to kv. The
 * logits of the last row go to logits (VOCAB floats). False when t = 0, the context would overflow, or
 * memory runs out. */
bool mesh_gpt2_forward(const float *w, const mesh_gpt2_layout *lay, const uint32_t *tokens, size_t t,
                       mesh_gpt2_kv *kv, size_t threads, float *logits);
bool mesh_gpt2_forward_layers(const float *w, const mesh_gpt2_layout *lay, const uint32_t *tokens, size_t t,
                              mesh_gpt2_kv *kv, size_t threads, mesh_gpt2_after_layer after, void *ctx,
                              float *logits);

/* A greedy generation: prefill `prompt`, then `steps` decode steps. toks gets steps + 1 tokens (the
 * first from the prefill), *digest the digest of every step's logits (prefill's included), and kv
 * (initialized here; free it) the cache. */
bool mesh_gpt2_generate(const float *w, const mesh_gpt2_layout *lay, const uint32_t *prompt, size_t n,
                        size_t steps, size_t threads, uint32_t *toks, uint64_t *digest, mesh_gpt2_kv *kv);

#endif
