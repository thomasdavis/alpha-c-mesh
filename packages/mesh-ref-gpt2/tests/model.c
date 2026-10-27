/* Golden data from the Rust (mesh-ref gpt2.rs) on synthetic weights: the row operations on hashed data
 * (layer norm, linear with each epilogue and a column range, attention over 1..600 keys, argmax, the
 * digest), the synthetic weights, a greedy generation (prompt 11 7, one step), and gpt2.rs's
 * decode_rows_equal_prefill_rows: three tokens one at a time give the KV and logits of the three at
 * once, both the Rust's. */
#include "mesh/ref/gpt2.h"

#include "mesh/ftz.h"
#include "mesh/ref/gpt2_attend.h"
#include "mesh/ref/gpt2_linear.h"
#include "mesh/ref/gpt2_logits.h"
#include "mesh/ref/gpt2_norm.h"
#include "mesh/ref/gpt2_weights.h"
#include "mesh/ref/matmul.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

static void want(const char *what, uint64_t got, uint64_t want) {
    if (got != want) {
        fprintf(stderr, "%s: %#018llx, want %#018llx\n", what, (unsigned long long)got, (unsigned long long)want);
        failures++;
    }
}

static uint64_t fh(uint64_t h, uint64_t w) { return (h ^ w) * 0x100000001b3ull; }

static float *mat(uint32_t seed, size_t n) {
    float *m = malloc(n * sizeof *m);
    mesh_matmul_matrix(seed, n, m);
    return m;
}

static void rows(void) {
    uint64_t h = 0xcbf29ce484222325ull;
    float *x = mat(1, 768), *g = mat(2, 768), *b = mat(3, 768), out[768];
    mesh_gpt2_normalize_row(x, g, b, out);
    for (int i = 0; i < 768; i++) h = fh(h, mesh_ftz_bits(out[i]));
    float *w = mat(4, 768 * 96), *bias = mat(5, 96);
    const mesh_gpt2_epi epis[3] = {MESH_GPT2_EPI_BIAS, MESH_GPT2_EPI_BIAS_GELU, MESH_GPT2_EPI_BIAS_RESIDUAL};
    for (int e = 0; e < 3; e++) {
        float *o = mat(6, 96), *o2 = mat(6, 40);
        mesh_gpt2_project_row(x, 768, w, bias, 96, epis[e], o);
        for (int i = 0; i < 96; i++) h = fh(h, mesh_ftz_bits(o[i]));
        mesh_gpt2_project_cols(x, 768, w, bias, 96, epis[e], 50, 40, o2);
        for (int i = 0; i < 40; i++) h = fh(h, mesh_ftz_bits(o2[i]));
        free(o), free(o2);
    }
    const size_t ns[6] = {1, 2, 7, 256, 257, 600};
    for (int t = 0; t < 6; t++) {
        float *q = mat(7, 64), *ks = mat(8, ns[t] * 64), *vs = mat(9, ns[t] * 64), o[64];
        mesh_gpt2_attend_row(q, ns[t], ks, 64, vs, 64, o);
        for (int i = 0; i < 64; i++) h = fh(h, mesh_ftz_bits(o[i]));
        free(q), free(ks), free(vs);
    }
    float *lg = mat(10, 1000);
    h = fh(h, mesh_gpt2_argmax(lg, 1000));
    h = fh(h, mesh_gpt2_digest_extend(MESH_GPT2_DIGEST_SEED, lg, 1000));
    free(x), free(g), free(b), free(w), free(bias), free(lg);
    want("rows", h, 0x2212298fd900ec07ull);
}

int main(void) {
    rows();
    mesh_gpt2_layout lay;
    mesh_gpt2_layout_init(&lay);
    float *w = mesh_gpt2_synthetic(5);
    if (!w) {
        fprintf(stderr, "no memory for the weights\n");
        return 1;
    }
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < lay.total; i += 997) h = fh(h, mesh_ftz_bits(w[i]));
    want("synthetic", h, 0x86cd90d5c4c081b6ull);

    const uint32_t prompt[2] = {11, 7};
    uint32_t toks[2];
    uint64_t digest;
    mesh_gpt2_kv kv;
    if (!mesh_gpt2_generate(w, &lay, prompt, 2, 1, 8, toks, &digest, &kv)) return 1;
    if (toks[0] != 48384 || toks[1] != 48384) fprintf(stderr, "tokens %u %u\n", toks[0], toks[1]), failures++;
    want("generate", digest, 0x9fc013029dba45d5ull);
    want("generate kv", mesh_gpt2_kv_digest(&kv), 0xfc7a3e32394ce039ull);
    mesh_gpt2_kv_free(&kv);

    /* decode_rows_equal_prefill_rows, three tokens */
    const uint32_t three[3] = {11, 7, 50256};
    float *la = malloc(MESH_GPT2_VOCAB * sizeof *la), *lb = malloc(MESH_GPT2_VOCAB * sizeof *lb);
    mesh_gpt2_kv a, b;
    mesh_gpt2_kv_init(&a);
    mesh_gpt2_kv_init(&b);
    if (!mesh_gpt2_forward(w, &lay, three, 3, &a, 8, la)) return 1;
    for (int t = 0; t < 3; t++)
        if (!mesh_gpt2_forward(w, &lay, &three[t], 1, &b, 3, lb)) return 1;
    want("prefill kv", mesh_gpt2_kv_digest(&a), 0xdddcc2e98df017bfull);
    want("decode kv", mesh_gpt2_kv_digest(&b), 0xdddcc2e98df017bfull);
    want("prefill logits", mesh_gpt2_digest_extend(MESH_GPT2_DIGEST_SEED, la, MESH_GPT2_VOCAB), 0xa7b4303cf98c5170ull);
    want("decode logits", mesh_gpt2_digest_extend(MESH_GPT2_DIGEST_SEED, lb, MESH_GPT2_VOCAB), 0xa7b4303cf98c5170ull);
    mesh_gpt2_kv_free(&a);
    mesh_gpt2_kv_free(&b);
    free(la), free(lb), free(w);
    return failures != 0;
}
