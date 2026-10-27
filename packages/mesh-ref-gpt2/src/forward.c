/* GPT-2's forward pass on the CPU (mesh-ref gpt2.rs `forward_layers`, `generate`, `par_linear`,
 * `par_rows`, `project_vocab`). The threads only split independent outputs. */
#include "mesh/ref/gpt2.h"

#include "mesh/ftz.h"
#include "mesh/ref/gpt2_attend.h"
#include "mesh/ref/gpt2_linear.h"
#include "mesh/ref/gpt2_logits.h"
#include "mesh/ref/gpt2_norm.h"
#include "mesh/ref/matmul.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define W MESH_GPT2_WIDTH
#define HD MESH_GPT2_HEAD_DIM

/* ---- a pool of units, each an index, taken from a shared counter by up to `threads` threads ---- */

typedef void (*unit_fn)(size_t unit, void *ctx);

typedef struct {
    unit_fn fn;
    void *ctx;
    size_t units;
    atomic_size_t next;
} pool;

static void *pool_worker(void *arg) {
    pool *p = arg;
    for (size_t u; (u = atomic_fetch_add(&p->next, 1)) < p->units;) p->fn(u, p->ctx);
    return NULL;
}

static void run_units(size_t units, size_t threads, unit_fn fn, void *ctx) {
    pool p = {fn, ctx, units, 0};
    if (threads > units) threads = units;
    if (threads <= 1) {
        pool_worker(&p);
        return;
    }
    pthread_t tid[threads];
    size_t started = 0;
    for (size_t i = 0; i < threads; i++)
        if (pthread_create(&tid[started], NULL, pool_worker, &p) == 0) started++;
    pool_worker(&p); /* this thread works too, and finishes whatever no thread took */
    for (size_t i = 0; i < started; i++) pthread_join(tid[i], NULL);
}

/* ---- a linear layer over rows: (row, column block) units ---- */

typedef struct {
    const float *x, *w, *b;
    size_t kd, nout, bw, blocks;
    mesh_gpt2_epi epi;
    float *out;
} linear;

static void linear_unit(size_t u, void *ctx) {
    linear *c = ctx;
    size_t r = u / c->blocks, c0 = (u % c->blocks) * c->bw;
    size_t n = c0 + c->bw <= c->nout ? c->bw : c->nout - c0;
    mesh_gpt2_project_cols(c->x + r * c->kd, c->kd, c->w, c->b, c->nout, c->epi, c0, n, c->out + r * c->nout + c0);
}

static void par_linear(const float *x, size_t kd, const float *w, const float *b, size_t nout, mesh_gpt2_epi epi,
                       float *out, size_t rows, size_t threads) {
    size_t blocks = ((threads ? threads : 1) * 4 + rows - 1) / rows;
    size_t max_blocks = (nout + 63) / 64;
    if (blocks < 1) blocks = 1;
    if (blocks > max_blocks) blocks = max_blocks;
    size_t bw = (nout + blocks - 1) / blocks;
    blocks = (nout + bw - 1) / bw;
    linear c = {x, w, b, kd, nout, bw, blocks, epi, out};
    run_units(rows * blocks, threads, linear_unit, &c);
}

/* ---- row-wise steps ---- */

typedef struct {
    const float *x, *g, *b;
    float *out;
} norm_ctx;

static void norm_unit(size_t r, void *ctx) {
    norm_ctx *c = ctx;
    mesh_gpt2_normalize_row(c->x + r * W, c->g, c->b, c->out + r * W);
}

typedef struct {
    const float *qkv, *kc, *vc;
    size_t p0;
    float *y;
} attend_ctx;

static void attend_unit(size_t r, void *ctx) {
    attend_ctx *c = ctx;
    for (size_t hh = 0; hh < MESH_GPT2_HEADS; hh++)
        mesh_gpt2_attend_row(c->qkv + r * 3 * W + hh * HD, c->p0 + r + 1, c->kc + hh * HD, W, c->vc + hh * HD, W,
                             c->y + r * W + hh * HD);
}

typedef struct {
    const float *wte, *h;
    float *out;
} vocab_ctx;

static void vocab_unit(size_t i, void *ctx) {
    vocab_ctx *c = ctx;
    mesh_matvec(c->wte + i * W, c->h, 1, W, &c->out[i]);
}

void mesh_gpt2_project_vocab(const float *wte, const float *h, size_t threads, float *out) {
    vocab_ctx c = {wte, h, out};
    run_units(MESH_GPT2_VOCAB, threads, vocab_unit, &c);
}

bool mesh_gpt2_forward(const float *w, const mesh_gpt2_layout *lay, const uint32_t *tokens, size_t t,
                       mesh_gpt2_kv *kv, size_t threads, float *logits) {
    return mesh_gpt2_forward_layers(w, lay, tokens, t, kv, threads, NULL, NULL, logits);
}

bool mesh_gpt2_forward_layers(const float *w, const mesh_gpt2_layout *lay, const uint32_t *tokens, size_t t,
                              mesh_gpt2_kv *kv, size_t threads, mesh_gpt2_after_layer after, void *ctx,
                              float *logits) {
    size_t p0 = mesh_gpt2_kv_len(kv);
    if (t == 0 || p0 + t > MESH_GPT2_CTX || !mesh_gpt2_kv_reserve(kv, p0 + t)) return false;
    float *x = malloc(t * W * sizeof *x), *h = malloc(t * W * sizeof *h), *qkv = malloc(t * 3 * W * sizeof *qkv),
          *y = malloc(t * W * sizeof *y), *f = malloc(t * MESH_GPT2_FF * sizeof *f), *last = malloc(W * sizeof *last);
    bool ok = x && h && qkv && y && f && last;
    if (ok) {
        for (size_t r = 0; r < t; r++) {
            size_t e = lay->wte + (size_t)tokens[r] * W, p = lay->wpe + (p0 + r) * W;
            for (size_t i = 0; i < W; i++) x[r * W + i] = mesh_ftz_add(w[e + i], w[p + i]);
        }
        for (size_t l = 0; l < MESH_GPT2_LAYERS; l++) {
            const mesh_gpt2_block_off *b = &lay->blocks[l];
            norm_ctx n1 = {x, w + b->ln1_g, w + b->ln1_b, h};
            run_units(t, threads, norm_unit, &n1);
            par_linear(h, W, w + b->attn_w, w + b->attn_b, 3 * W, MESH_GPT2_EPI_BIAS, qkv, t, threads);
            for (size_t r = 0; r < t; r++) {
                memcpy(kv->k[l] + (p0 + r) * W, qkv + r * 3 * W + W, W * sizeof(float));
                memcpy(kv->v[l] + (p0 + r) * W, qkv + r * 3 * W + 2 * W, W * sizeof(float));
            }
            kv->rows[l] = p0 + t;
            attend_ctx at = {qkv, kv->k[l], kv->v[l], p0, y};
            run_units(t, threads, attend_unit, &at);
            par_linear(y, W, w + b->proj_w, w + b->proj_b, W, MESH_GPT2_EPI_BIAS_RESIDUAL, x, t, threads);
            norm_ctx n2 = {x, w + b->ln2_g, w + b->ln2_b, h};
            run_units(t, threads, norm_unit, &n2);
            par_linear(h, W, w + b->fc_w, w + b->fc_b, MESH_GPT2_FF, MESH_GPT2_EPI_BIAS_GELU, f, t, threads);
            par_linear(f, MESH_GPT2_FF, w + b->fc2_w, w + b->fc2_b, W, MESH_GPT2_EPI_BIAS_RESIDUAL, x, t, threads);
            if (after) after(l, kv, ctx);
        }
        mesh_gpt2_normalize_row(x + (t - 1) * W, w + lay->lnf_g, w + lay->lnf_b, last);
        mesh_gpt2_project_vocab(w + lay->wte, last, threads, logits);
    }
    free(x), free(h), free(qkv), free(y), free(f), free(last);
    return ok;
}

bool mesh_gpt2_generate(const float *w, const mesh_gpt2_layout *lay, const uint32_t *prompt, size_t n,
                        size_t steps, size_t threads, uint32_t *toks, uint64_t *digest, mesh_gpt2_kv *kv) {
    mesh_gpt2_kv_init(kv);
    float *l = malloc(MESH_GPT2_VOCAB * sizeof *l);
    if (!l || !mesh_gpt2_forward(w, lay, prompt, n, kv, threads, l)) {
        free(l);
        return false;
    }
    uint64_t h = mesh_gpt2_digest_extend(MESH_GPT2_DIGEST_SEED, l, MESH_GPT2_VOCAB);
    toks[0] = mesh_gpt2_argmax(l, MESH_GPT2_VOCAB);
    for (size_t s = 0; s < steps; s++) {
        if (!mesh_gpt2_forward(w, lay, &toks[s], 1, kv, threads, l)) {
            free(l);
            return false;
        }
        h = mesh_gpt2_digest_extend(h, l, MESH_GPT2_VOCAB);
        toks[s + 1] = mesh_gpt2_argmax(l, MESH_GPT2_VOCAB);
    }
    free(l);
    *digest = h;
    return true;
}
