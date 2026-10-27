/* gpt2x on the simulator, op by op against mesh_ref::gpt2's pieces, bitwise:
 * the window (PUT, COPY), EMBED, NORM (normalize_row), GEMM and GEMV with
 * each epilogue (project_row, project_cols), KV and ATTN (attend_row), LOGITS
 * (a lane's sequential sum, then the warp butterfly), and the scalar
 * functions themselves (exp, tanh, gelu, sqrt). Synthetic data: a seeded
 * hash, not the real weights. */
#include "mesh/check.h"
#include "mesh/ref/block.h"
#include "mesh/ref/gpt2_attend.h"
#include "mesh/ref/gpt2_linear.h"
#include "mesh/ref/gpt2_norm.h"
#include "mesh/ref/gpt2_scalar.h"
#include "mesh/rt/sim_mailbox.h"

#include "../src/gpt2x.c"

#include <stdlib.h>

#define W GX_WIDTH
static uint8_t *mb, *view;
static float *sc;

/* Scratch layout (floats). */
#define O_WTE 0u
#define O_WPE (O_WTE + GX_VOCAB * W)
#define O_X (O_WPE + GX_CTX * W)
#define O_N (O_X + 8 * W)
#define O_G (O_N + 8 * W)
#define O_B (O_G + W)
#define O_A (O_B + W)
#define O_MW (O_A + 8 * 300)
#define O_BIAS (O_MW + 300 * 200)
#define O_C (O_BIAS + 200)
#define O_QKV (O_C + 8 * 200)
#define O_KC (O_QKV + 8 * 3 * W)
#define O_VC (O_KC + GX_CTX * W)
#define O_Y (O_VC + GX_CTX * W)
#define O_LOG (O_Y + 8 * W)
#define O_END (O_LOG + GX_VOCAB)

static float val(uint32_t seed, uint32_t i) {
    uint32_t h = mesh_block_mix(seed * 0x9e3779b9u + i * 0x85ebca6bu);
    return (float)(h >> 8) * (1.0f / 8388608.0f) - 1.0f;
}
static void fill(uint32_t off, uint32_t n, uint32_t seed, float scale) { for (uint32_t i = 0; i < n; i++) sc[off + i] = val(seed, i) * scale; }
static void params(const uint32_t *kv, size_t n) {
    for (uint32_t i = 0; i < MESH_GPT2X_PARAM_WORDS; i++) mesh_sim_set_u32(view + MESH_GPT2X_X_PARAMS + 4 * i, 0);
    for (size_t i = 0; i < n; i += 2) mesh_sim_set_u32(view + MESH_GPT2X_X_PARAMS + 4 * kv[i], kv[i + 1]);
}
static uint32_t run(uint32_t op, uint32_t arg) { return mesh_sim_call(mb, 0, op, arg); }
static uint32_t bits(float f) { return mesh_f32_bits(f); }

static void gemm_case(uint32_t m, uint32_t k, uint32_t n, uint32_t epi, bool gemv) {
    fill(O_A, m * k, 11 + epi, 1.0f);
    fill(O_MW, k * n, 12 + epi, 0.25f);
    fill(O_BIAS, n, 13 + epi, 0.5f);
    fill(O_C, m * n, 14 + epi, 1.0f);
    float *want = malloc(sizeof(float) * m * n);
    memcpy(want, sc + O_C, sizeof(float) * m * n);
    for (uint32_t r = 0; r < m; r++) mesh_gpt2_project_row(sc + O_A + r * k, k, sc + O_MW, sc + O_BIAS, n, (mesh_gpt2_epi)epi, want + r * n);
    const uint32_t kv[] = {MESH_GPT2X_P_A, O_A, MESH_GPT2X_P_B, O_MW, MESH_GPT2X_P_BIAS, O_BIAS, MESH_GPT2X_P_C, O_C,
                           MESH_GPT2X_P_M, m,   MESH_GPT2X_P_K, k,    MESH_GPT2X_P_N,    n,      MESH_GPT2X_P_EPI, epi};
    params(kv, sizeof kv / sizeof *kv);
    if (gemv) {
        uint32_t nb = (n + 63) / 64;
        for (uint32_t a = 0; a < m * nb; a++) CHECK_U(run(MESH_GPT2X_OP_GEMV, a), MESH_R_NONE);
        CHECK_U(run(MESH_GPT2X_OP_GEMV, m * nb), MESH_R_BAD_OP);
    } else {
        uint32_t tiles = ((m + 63) / 64) * ((n + 63) / 64);
        for (uint32_t t = 0; t < tiles; t++) CHECK_U(run(MESH_GPT2X_OP_GEMM, t), MESH_R_NONE);
        CHECK_U(run(MESH_GPT2X_OP_GEMM, tiles), MESH_R_BAD_OP);
    }
    uint32_t bad = 0;
    for (uint32_t i = 0; i < m * n; i++) bad += bits(sc[O_C + i]) != bits(want[i]);
    CHECK_U(bad, 0);
    free(want);
}

int main(void) {
    /* The scalar functions against the reference over a sweep. */
    uint32_t bad = 0;
    for (int i = -4000; i <= 4000; i++) {
        float x = (float)i * 0.0251f;
        bad += bits(gx_exp_poly(x)) != bits(mesh_gpt2_exp_poly(x));
        bad += bits(gx_tanh_poly(x)) != bits(mesh_gpt2_tanh_poly(x));
        bad += bits(gx_gelu_tanh(x)) != bits(mesh_gpt2_gelu_tanh(x));
        if (x > 0) bad += bits(gx_sqrt_newton(x)) != bits(mesh_gpt2_sqrt_newton(x));
    }
    CHECK_U(bad, 0);

    mb = mesh_sim_mailbox();
    sc = calloc(O_END, sizeof(float));
    CHECK(sc != NULL);
    mesh_sim_prog p = {.name = "gpt2x", .view_bytes = MESH_GPT2X_VIEW_BYTES, .nodes = 2, .warps = 8, .shared_bytes = 0x4200,
                       .scratch = (uint8_t *)sc, .scratch_bytes = 4u * O_END};
    view = mesh_sim_install(mb, 0, &p);
    mesh_sim_launch *l = mesh_sim_launch_prog(mb, 0, &p, mesh_main);
    CHECK_U(run(MESH_OP_INIT, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + MESH_GPT2X_X_MAGIC), MESH_GPT2X_MAGIC);
    /* PUT then COPY through the window. */
    for (uint32_t i = 0; i < 100; i++) mesh_sim_set_f32(view + MESH_GPT2X_X_WINDOW + 4 * i, (float)i * 1.5f);
    const uint32_t put[] = {MESH_GPT2X_P_OFF, O_X, MESH_GPT2X_P_LEN, 100};
    params(put, 4);
    CHECK_U(run(MESH_GPT2X_OP_PUT, 0), MESH_R_NONE);
    CHECK_U(bits(sc[O_X + 99]), bits(148.5f));
    for (uint32_t i = 0; i < 100; i++) mesh_sim_set_f32(view + MESH_GPT2X_X_WINDOW + 4 * i, 0.0f);
    CHECK_U(run(MESH_GPT2X_OP_COPY, 0), MESH_R_NONE);
    CHECK_U(mesh_sim_u32(view + MESH_GPT2X_X_WINDOW + 4 * 7), bits(10.5f));
    CHECK_U(run(MESH_GPT2X_OP_COPY, 1), MESH_R_BAD_OP);
    /* EMBED: token rows plus position rows. */
    fill(O_WTE, 4 * W, 1, 0.1f);
    fill(O_WTE + 50000 * W, W, 2, 0.1f);
    fill(O_WPE, 8 * W, 3, 0.01f);
    mesh_sim_set_u32(view + MESH_GPT2X_X_TOKENS, 3);
    mesh_sim_set_u32(view + MESH_GPT2X_X_TOKENS + 4, 50000);
    const uint32_t emb[] = {MESH_GPT2X_P_T, 2, MESH_GPT2X_P_P0, 5, MESH_GPT2X_P_WTE, O_WTE, MESH_GPT2X_P_WPE, O_WPE, MESH_GPT2X_P_X, O_X};
    params(emb, 10);
    CHECK_U(run(MESH_GPT2X_OP_EMBED, 0), MESH_R_NONE);
    CHECK_U(run(MESH_GPT2X_OP_EMBED, 1), MESH_R_NONE);
    CHECK_U(run(MESH_GPT2X_OP_EMBED, 2), MESH_R_BAD_OP);
    bad = 0;
    for (uint32_t i = 0; i < W; i++) {
        bad += bits(sc[O_X + i]) != bits(sc[O_WTE + 3 * W + i] + sc[O_WPE + 5 * W + i]);
        bad += bits(sc[O_X + W + i]) != bits(sc[O_WTE + 50000 * W + i] + sc[O_WPE + 6 * W + i]);
    }
    CHECK_U(bad, 0);
    /* NORM. */
    fill(O_X, 3 * W, 4, 2.0f);
    fill(O_G, W, 5, 1.0f);
    fill(O_B, W, 6, 0.1f);
    const uint32_t nrm[] = {MESH_GPT2X_P_A, O_X, MESH_GPT2X_P_B, O_G, MESH_GPT2X_P_BIAS, O_B, MESH_GPT2X_P_C, O_N, MESH_GPT2X_P_T, 3};
    params(nrm, 10);
    for (uint32_t r = 0; r < 3; r++) CHECK_U(run(MESH_GPT2X_OP_NORM, r), MESH_R_NONE);
    float want[W];
    bad = 0;
    for (uint32_t r = 0; r < 3; r++) {
        mesh_gpt2_normalize_row(sc + O_X + r * W, sc + O_G, sc + O_B, want);
        for (uint32_t i = 0; i < W; i++) bad += bits(sc[O_N + r * W + i]) != bits(want[i]);
    }
    CHECK_U(bad, 0);
    /* GEMM and GEMV with each epilogue (dimensions off the tile grid). */
    for (uint32_t epi = 0; epi < 3; epi++) {
        gemm_case(5, 300, 130, epi, false);
        gemm_case(3, 300, 200, epi, true);
    }
    /* KV and ATTN: 4 rows at positions 9..12, causal over the cache. */
    fill(O_QKV, 4 * 3 * W, 7, 1.0f);
    fill(O_KC, 9 * W, 8, 1.0f);
    fill(O_VC, 9 * W, 9, 1.0f);
    const uint32_t kvp[] = {MESH_GPT2X_P_T, 4, MESH_GPT2X_P_P0, 9, MESH_GPT2X_P_QKV, O_QKV, MESH_GPT2X_P_KC, O_KC, MESH_GPT2X_P_VC, O_VC,
                            MESH_GPT2X_P_Y, O_Y, MESH_GPT2X_P_QB, 3};
    params(kvp, 14);
    for (uint32_t r = 0; r < 4; r++) CHECK_U(run(MESH_GPT2X_OP_KV, r), MESH_R_NONE);
    uint32_t nq = 2; /* ceil(4 / 3) */
    for (uint32_t a = 0; a < GX_HEADS * nq; a++) CHECK_U(run(MESH_GPT2X_OP_ATTN, a), MESH_R_NONE);
    CHECK_U(run(MESH_GPT2X_OP_ATTN, GX_HEADS * nq), MESH_R_BAD_OP);
    bad = 0;
    for (uint32_t r = 0; r < 4; r++)
        for (uint32_t h = 0; h < GX_HEADS; h++) {
            float out[GX_HEAD_DIM];
            uint32_t hd = h * GX_HEAD_DIM;
            CHECK(mesh_gpt2_attend_row(sc + O_QKV + r * 3 * W + hd, 9 + r + 1, sc + O_KC + hd, W, sc + O_VC + hd, W, out));
            for (uint32_t d = 0; d < GX_HEAD_DIM; d++) bad += bits(sc[O_Y + r * W + hd + d]) != bits(out[d]);
        }
    CHECK_U(bad, 0);
    /* LOGITS: the first blocks and the last (rows past VOCAB clamped). */
    fill(O_N, W, 10, 1.0f);
    fill(O_WTE, 600 * W, 15, 0.05f);
    const uint32_t lg[] = {MESH_GPT2X_P_A, O_N, MESH_GPT2X_P_B, O_WTE, MESH_GPT2X_P_C, O_LOG};
    params(lg, 6);
    uint32_t last = (GX_VOCAB - 1) / MESH_GPT2X_LOGIT_ROWS;
    const uint32_t args[3] = {0, 1, last};
    for (int i = 0; i < 3; i++) CHECK_U(run(MESH_GPT2X_OP_LOGITS, args[i]), MESH_R_NONE);
    CHECK_U(run(MESH_GPT2X_OP_LOGITS, last + 1), MESH_R_BAD_OP);
    bad = 0;
    for (int ai = 0; ai < 3; ai++)
        for (uint32_t r = args[ai] * MESH_GPT2X_LOGIT_ROWS; r < (args[ai] + 1) * MESH_GPT2X_LOGIT_ROWS && r < GX_VOCAB; r++) {
            float lanes[32];
            for (uint32_t ln = 0; ln < 32; ln++) {
                float a = 0.0f;
                for (uint32_t k = ln; k < W; k += 32) a = a + sc[O_WTE + r * W + k] * sc[O_N + k];
                lanes[ln] = a;
            }
            mesh_block_warp_reduce_f32(lanes, mesh_block_add_f32);
            bad += bits(sc[O_LOG + r]) != bits(lanes[0]);
        }
    CHECK_U(bad, 0);
    CHECK_U(run(MESH_OP_TASK, 0), MESH_R_BAD_OP);
    CHECK_U(mesh_sim_u32(mesh_sim_ctl(mb, 0) + MESH_P_FAULT), 0);
    mesh_sim_halt(mb, 0, l);
    free(sc);
    mesh_sim_mailbox_free(mb);
    return mesh_check_done();
}
