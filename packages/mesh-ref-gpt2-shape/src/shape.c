/* GPT-2 124M's weight layout (mesh-ref gpt2.rs `Layout`). */
#include "mesh/ref/gpt2_shape.h"

void mesh_gpt2_layout_init(mesh_gpt2_layout *l) {
    size_t at = 0;
#define TAKE(n) (at += (n), at - (n))
    l->wte = TAKE((size_t)MESH_GPT2_VOCAB * MESH_GPT2_WIDTH);
    l->wpe = TAKE((size_t)MESH_GPT2_CTX * MESH_GPT2_WIDTH);
    for (int i = 0; i < MESH_GPT2_LAYERS; i++) {
        mesh_gpt2_block_off *b = &l->blocks[i];
        b->ln1_g = TAKE(MESH_GPT2_WIDTH);
        b->ln1_b = TAKE(MESH_GPT2_WIDTH);
        b->attn_w = TAKE((size_t)MESH_GPT2_WIDTH * 3 * MESH_GPT2_WIDTH);
        b->attn_b = TAKE(3 * MESH_GPT2_WIDTH);
        b->proj_w = TAKE((size_t)MESH_GPT2_WIDTH * MESH_GPT2_WIDTH);
        b->proj_b = TAKE(MESH_GPT2_WIDTH);
        b->ln2_g = TAKE(MESH_GPT2_WIDTH);
        b->ln2_b = TAKE(MESH_GPT2_WIDTH);
        b->fc_w = TAKE((size_t)MESH_GPT2_WIDTH * MESH_GPT2_FF);
        b->fc_b = TAKE(MESH_GPT2_FF);
        b->fc2_w = TAKE((size_t)MESH_GPT2_FF * MESH_GPT2_WIDTH);
        b->fc2_b = TAKE(MESH_GPT2_WIDTH);
    }
    l->lnf_g = TAKE(MESH_GPT2_WIDTH);
    l->lnf_b = TAKE(MESH_GPT2_WIDTH);
    l->total = at;
#undef TAKE
}

static void put(char *name, size_t *at, const char *s) {
    while (*s && *at < 31) name[(*at)++] = *s++;
    name[*at] = 0;
}

bool mesh_gpt2_tensor(const mesh_gpt2_layout *l, size_t i, char name[32], size_t *off, size_t *count) {
    size_t at = 0;
    name[0] = 0;
    if (i == 0) return put(name, &at, "wte.weight"), *off = l->wte, *count = (size_t)MESH_GPT2_VOCAB * MESH_GPT2_WIDTH, true;
    if (i == 1) return put(name, &at, "wpe.weight"), *off = l->wpe, *count = (size_t)MESH_GPT2_CTX * MESH_GPT2_WIDTH, true;
    if (i == MESH_GPT2_TENSORS - 2) return put(name, &at, "ln_f.weight"), *off = l->lnf_g, *count = MESH_GPT2_WIDTH, true;
    if (i == MESH_GPT2_TENSORS - 1) return put(name, &at, "ln_f.bias"), *off = l->lnf_b, *count = MESH_GPT2_WIDTH, true;
    if (i >= MESH_GPT2_TENSORS) return false;
    size_t layer = (i - 2) / 12, k = (i - 2) % 12;
    const mesh_gpt2_block_off *b = &l->blocks[layer];
    static const char *const names[12] = {"ln_1.weight",        "ln_1.bias",          "attn.c_attn.weight",
                                          "attn.c_attn.bias",   "attn.c_proj.weight", "attn.c_proj.bias",
                                          "ln_2.weight",        "ln_2.bias",          "mlp.c_fc.weight",
                                          "mlp.c_fc.bias",      "mlp.c_proj.weight",  "mlp.c_proj.bias"};
    const size_t offs[12] = {b->ln1_g, b->ln1_b, b->attn_w, b->attn_b, b->proj_w, b->proj_b,
                             b->ln2_g, b->ln2_b, b->fc_w,   b->fc_b,   b->fc2_w,  b->fc2_b};
    const size_t counts[12] = {MESH_GPT2_WIDTH, MESH_GPT2_WIDTH, (size_t)MESH_GPT2_WIDTH * 3 * MESH_GPT2_WIDTH,
                               3 * MESH_GPT2_WIDTH, (size_t)MESH_GPT2_WIDTH * MESH_GPT2_WIDTH, MESH_GPT2_WIDTH,
                               MESH_GPT2_WIDTH, MESH_GPT2_WIDTH, (size_t)MESH_GPT2_WIDTH * MESH_GPT2_FF,
                               MESH_GPT2_FF, (size_t)MESH_GPT2_FF * MESH_GPT2_WIDTH, MESH_GPT2_WIDTH};
    char num[4] = {0};
    if (layer >= 10) num[0] = (char)('0' + layer / 10), num[1] = (char)('0' + layer % 10);
    else num[0] = (char)('0' + layer);
    put(name, &at, "h.");
    put(name, &at, num);
    put(name, &at, ".");
    put(name, &at, names[k]);
    *off = offs[k];
    *count = counts[k];
    return true;
}
