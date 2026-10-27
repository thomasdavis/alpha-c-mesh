/* @ajax/mesh-ref-gpt2-shape: GPT-2 124M's shape and the flat weight buffer's layout (mesh-ref gpt2.rs):
 * every tensor in checkpoint order and shape (wte, wpe, the blocks, ln_f), the same on the CPU and in
 * the GPU's scratch. Weights are [in][out] as the checkpoint stores them (HF Conv1D). Freestanding. */
#ifndef MESH_REF_GPT2_SHAPE_H
#define MESH_REF_GPT2_SHAPE_H

#include <stdbool.h>
#include <stddef.h>

#define MESH_GPT2_LAYERS 12
#define MESH_GPT2_HEADS 12
#define MESH_GPT2_HEAD_DIM 64
#define MESH_GPT2_WIDTH 768
#define MESH_GPT2_FF 3072
#define MESH_GPT2_VOCAB 50257
#define MESH_GPT2_CTX 1024
/* The threads a row's reductions are split over (the GPU node: 8 warps). */
#define MESH_GPT2_NT 256
/* The groups of the attention output's sum over keys (NT / HEAD_DIM). */
#define MESH_GPT2_PV_GROUPS (MESH_GPT2_NT / MESH_GPT2_HEAD_DIM)
/* KV values per token: layers x (K, V) x width. */
#define MESH_GPT2_KV_PER_TOKEN (MESH_GPT2_LAYERS * 2 * MESH_GPT2_WIDTH)
/* Tensors in the checkpoint: wte, wpe, 12 per block, ln_f weight and bias. */
#define MESH_GPT2_TENSORS (2 + 12 * MESH_GPT2_LAYERS + 2)

/* Offsets (in floats) of one block's tensors in the flat weight buffer. */
typedef struct {
    size_t ln1_g, ln1_b, attn_w, attn_b, proj_w, proj_b, ln2_g, ln2_b, fc_w, fc_b, fc2_w, fc2_b;
} mesh_gpt2_block_off;

typedef struct {
    size_t wte, wpe;
    mesh_gpt2_block_off blocks[MESH_GPT2_LAYERS];
    size_t lnf_g, lnf_b, total;
} mesh_gpt2_layout;

void mesh_gpt2_layout_init(mesh_gpt2_layout *l);
/* Tensor i (0..MESH_GPT2_TENSORS) in buffer order: its checkpoint name ("h.3.attn.c_attn.weight", at
 * most 31 characters plus NUL), offset and element count. False if i is out of range. */
bool mesh_gpt2_tensor(const mesh_gpt2_layout *l, size_t i, char name[32], size_t *off, size_t *count);

#endif
