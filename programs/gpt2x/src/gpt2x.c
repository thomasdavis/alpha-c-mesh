/* gpt2x: GPT-2 124M's exact-mode kernels (program.toml). Every op follows
 * mesh_ref::gpt2 operation for operation -- the same sums in the same order,
 * every f32 op correctly rounded, FTZ and unfused, exp / tanh / sqrt from
 * + - * / -- so the logits and the KV cache are bitwise the reference's on
 * every GPU. A node is 8 warps (256 threads). The host (meshd's gpt2x) runs
 * the model as stages of tasks: it writes a stage's operands to the parameter
 * block, publishes its tasks (the arg is the unit of work: a row, a tile, a
 * head and query block), and waits for all of them before the next stage.
 * Layouts: @ajax/mesh-abi-view-gpt2x.
 * Ported from alpha-rust-mesh programs/gpt2x/src/lib.rs. */
#include "mesh/rt.h"
#include "mesh/rt/linalg.h"

#define GX_WIDTH 768u
#define GX_HEAD_DIM 64u
#define GX_HEADS 12u
#define GX_VOCAB 50257u
#define GX_CTX 1024u
#define GX_PV_GROUPS 4u

/* mesh_ref::gpt2's constants (the same bits). */
#define GX_ATTN_SCALE 0.125f
#define GX_LN_EPS_BITS 0x3727c5acu
#define GX_GELU_C_BITS 0x3f4c422au
#define GX_GELU_A_BITS 0x3d372713u
#define GX_LOG2E_BITS 0x3fb8aa3bu
#define GX_LN2_HI_BITS 0x3f317200u
#define GX_LN2_LO_BITS 0x35bfbe8eu
static const uint32_t GX_EXP_POLY[11] = {
    0x3493f27eu, 0x3638ef1du, 0x37d00d01u, 0x39500d01u, 0x3ab60b61u, 0x3c088889u, 0x3d2aaaabu, 0x3e2aaaabu, 0x3f000000u, 0x3f800000u, 0x3f800000u,
};

/* Shared memory (bytes): the parameter block, then the op's own. */
#define GX_SH_PARAMS 0u
#define GX_SH_OP 128u
/* ATTN: q (64), scores (CTX), the PV partials (256). */
#define GX_SH_Q GX_SH_OP
#define GX_SH_S (GX_SH_Q + 4u * GX_HEAD_DIM)
#define GX_SH_PART (GX_SH_S + 4u * GX_CTX)

#define GX_INLINE static inline __attribute__((always_inline))

/* mesh_ref::gpt2::exp_poly. */
GX_INLINE float gx_exp_poly(float x) {
    if (x < -87.0f) return 0.0f;
    if (x > 88.0f) return __builtin_inff();
    float t = x * mesh_f32_from_bits(GX_LOG2E_BITS);
    float s = t >= 0.0f ? t + 0.5f : t - 0.5f;
    int32_t k = (int32_t)s;
    float kf = (float)k;
    float r = (x - kf * mesh_f32_from_bits(GX_LN2_HI_BITS)) - kf * mesh_f32_from_bits(GX_LN2_LO_BITS);
    float p = mesh_f32_from_bits(GX_EXP_POLY[0]);
    for (int i = 1; i < 11; i++) p = p * r + mesh_f32_from_bits(GX_EXP_POLY[i]);
    int32_t k1 = k / 2, k2 = k - k1;
    return p * mesh_f32_from_bits((uint32_t)(k1 + 127) << 23) * mesh_f32_from_bits((uint32_t)(k2 + 127) << 23);
}
/* mesh_ref::gpt2::tanh_poly. */
GX_INLINE float gx_tanh_poly(float x) {
    if (x > 10.0f) return 1.0f;
    if (x < -10.0f) return -1.0f;
    return 1.0f - 2.0f / (gx_exp_poly(x * 2.0f) + 1.0f);
}
/* mesh_ref::gpt2::gelu_tanh. */
GX_INLINE float gx_gelu_tanh(float x) {
    float inner = mesh_f32_from_bits(GX_GELU_C_BITS) * (x + mesh_f32_from_bits(GX_GELU_A_BITS) * x * x * x);
    return 0.5f * x * (1.0f + gx_tanh_poly(inner));
}
/* mesh_ref::gpt2::sqrt_newton. */
GX_INLINE float gx_sqrt_newton(float a) {
    float y = mesh_f32_from_bits((mesh_f32_bits(a) >> 1) + 0x1fbd1df5u);
    for (int i = 0; i < 4; i++) y = 0.5f * (y + a / y);
    return y;
}

/* The scratch as f32s, and its length. */
typedef struct {
    float *p;
    uint32_t n;
} gx_s;
/* [off, off + len) inside the scratch. */
GX_INLINE bool gx_fits(const gx_s *s, uint32_t off, uint32_t len) { return (uint64_t)off + len <= s->n; }
GX_INLINE float gx_rd(const gx_s *s, uint32_t i) { return s->p[i]; }
GX_INLINE void gx_wr(const gx_s *s, uint32_t i, float v) { s->p[i] = v; }

static void gx_init(const mesh_ctx *ctx) {
    if (mesh_ctx_leader(ctx)) mesh_ctx_set_u32(ctx, MESH_GPT2X_X_MAGIC, MESH_GPT2X_MAGIC);
}

/* The parameter block: thread 0 reads the view once (on a PCIe GPU every read
 * of the mailbox crosses the bus), the node reads shared memory. */
static void gx_params(const mesh_ctx *ctx, mesh_shared sh, uint32_t p[MESH_GPT2X_PARAM_WORDS]) {
    if (mesh_ctx_leader(ctx)) {
        mesh_view v = mesh_ctx_view(ctx);
        for (uint32_t i = 0; i < MESH_GPT2X_PARAM_WORDS; i++) mesh_shared_set_unchecked_u32(sh, i, mesh_view_u32(v, MESH_GPT2X_X_PARAMS + 4 * i));
    }
    mesh_ctx_block_barrier(ctx);
    for (uint32_t i = 0; i < MESH_GPT2X_PARAM_WORDS; i++) p[i] = mesh_shared_get_unchecked_u32(sh, i);
}

/* The epilogue (mesh_ref::gpt2::Epi) of output (r, c) of C (ldc n). */
typedef struct {
    const gx_s *s;
    uint32_t epi, bias, c_off, n, row;
} gx_finish;
GX_INLINE void gx_finish_at(const gx_finish *f, uint32_t r, uint32_t c, float acc) {
    float v = acc + gx_rd(f->s, f->bias + c);
    uint32_t at = f->c_off + r * f->n + c;
    float out = f->epi == MESH_GPT2X_EPI_BIAS_GELU ? gx_gelu_tanh(v) : f->epi == MESH_GPT2X_EPI_BIAS_RESIDUAL ? gx_rd(f->s, at) + v : v;
    gx_wr(f->s, at, out);
}
static void gx_finish2(uint32_t r, uint32_t c, float acc, void *user) { gx_finish_at(user, r, c, acc); }
static void gx_finish1(uint32_t c, float acc, void *user) {
    const gx_finish *f = user;
    gx_finish_at(f, f->row, c, acc);
}

/* Row layer normalization (mesh_ref::gpt2::normalize_row). */
static void gx_normalize(const mesh_ctx *ctx, const gx_s *s, uint32_t src, uint32_t dst, uint32_t g, uint32_t b) {
    uint32_t t = mesh_ctx_thread(ctx);
    float part = 0.0f;
    for (uint32_t i = t; i < GX_WIDTH; i += MESH_LINALG_TILE_THREADS) part = part + gx_rd(s, src + i);
    float mean = mesh_ctx_block_sum_f32(ctx, part) / (float)GX_WIDTH;
    float q = 0.0f;
    for (uint32_t i = t; i < GX_WIDTH; i += MESH_LINALG_TILE_THREADS) {
        float d = gx_rd(s, src + i) - mean;
        q = q + d * d;
    }
    float var = mesh_ctx_block_sum_f32(ctx, q) / (float)GX_WIDTH;
    float inv = 1.0f / gx_sqrt_newton(var + mesh_f32_from_bits(GX_LN_EPS_BITS));
    for (uint32_t i = t; i < GX_WIDTH; i += MESH_LINALG_TILE_THREADS)
        gx_wr(s, dst + i, (gx_rd(s, src + i) - mean) * inv * gx_rd(s, g + i) + gx_rd(s, b + i));
}

/* One head's causal attention for query row r at position pos (mesh_ref::gpt2::attend_row). */
static void gx_attend(const mesh_ctx *ctx, const gx_s *s, const uint32_t p[MESH_GPT2X_PARAM_WORDS], uint32_t h, uint32_t r, uint32_t pos,
                      mesh_shared sq, mesh_shared ss, mesh_shared sp) {
    uint32_t qkv = p[MESH_GPT2X_P_QKV], kc = p[MESH_GPT2X_P_KC], vc = p[MESH_GPT2X_P_VC], y = p[MESH_GPT2X_P_Y];
    uint32_t t = mesh_ctx_thread(ctx), n = pos + 1, hd = h * GX_HEAD_DIM;
    if (t < GX_HEAD_DIM) mesh_shared_set_unchecked_f32(sq, t, gx_rd(s, qkv + r * 3 * GX_WIDTH + hd + t));
    mesh_ctx_block_barrier(ctx);
    float lmax = mesh_f32_from_bits(MESH_RT_MAX_F32_ID_BITS);
    for (uint32_t j = t; j < n; j += MESH_LINALG_TILE_THREADS) {
        uint32_t k = kc + j * GX_WIDTH + hd;
        float a = 0.0f;
        for (uint32_t d = 0; d < GX_HEAD_DIM; d++) a = a + mesh_shared_get_unchecked_f32(sq, d) * gx_rd(s, k + d);
        float sc = a * GX_ATTN_SCALE;
        mesh_shared_set_unchecked_f32(ss, j, sc);
        lmax = mesh_rt_max_f32(lmax, sc);
    }
    float m = mesh_ctx_block_max_f32(ctx, lmax);
    float lsum = 0.0f;
    for (uint32_t j = t; j < n; j += MESH_LINALG_TILE_THREADS) {
        float e = gx_exp_poly(mesh_shared_get_unchecked_f32(ss, j) - m);
        mesh_shared_set_unchecked_f32(ss, j, e);
        lsum = lsum + e;
    }
    float sum = mesh_ctx_block_sum_f32(ctx, lsum);
    for (uint32_t j = t; j < n; j += MESH_LINALG_TILE_THREADS) mesh_shared_set_unchecked_f32(ss, j, mesh_shared_get_unchecked_f32(ss, j) / sum);
    mesh_ctx_block_barrier(ctx);
    uint32_t g = t / GX_HEAD_DIM, d = t % GX_HEAD_DIM;
    float a = 0.0f;
    for (uint32_t j = g; j < n; j += GX_PV_GROUPS) a = a + mesh_shared_get_unchecked_f32(ss, j) * gx_rd(s, vc + j * GX_WIDTH + hd + d);
    mesh_shared_set_unchecked_f32(sp, t, a);
    mesh_ctx_block_barrier(ctx);
    if (t < GX_HEAD_DIM) {
        float tot = mesh_shared_get_unchecked_f32(sp, t);
        for (uint32_t gg = 1; gg < GX_PV_GROUPS; gg++) tot = tot + mesh_shared_get_unchecked_f32(sp, gg * GX_HEAD_DIM + t);
        gx_wr(s, y + r * GX_WIDTH + hd + t, tot);
    }
    mesh_ctx_block_barrier(ctx);
}

static uint32_t gx_div_ceil(uint32_t a, uint32_t b) { return a / b + (a % b != 0); }

static uint32_t gx_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    if (op == MESH_OP_INIT) {
        gx_init(ctx);
        return MESH_R_NONE;
    }
    uint32_t len;
    uint8_t *sp = mesh_ctx_scratch_ptr(ctx, &len);
    if (!sp || mesh_ctx_threads(ctx) != MESH_LINALG_TILE_THREADS) return MESH_R_BAD_OP;
    gx_s s = {(float *)(void *)sp, len / 4};
    mesh_shared shp;
    if (!mesh_ctx_shared(ctx, GX_SH_PARAMS, MESH_GPT2X_PARAM_WORDS, 4, &shp)) return MESH_R_BAD_OP;
    uint32_t p[MESH_GPT2X_PARAM_WORDS];
    gx_params(ctx, shp, p);
    bool ok;
    switch (op) {
    case MESH_GPT2X_OP_PUT:
    case MESH_GPT2X_OP_COPY: {
        uint32_t off = p[MESH_GPT2X_P_OFF], total = p[MESH_GPT2X_P_LEN];
        uint32_t lo = arg * MESH_GPT2X_CHUNK;
        if (lo >= total || total > MESH_GPT2X_WINDOW_FLOATS || !gx_fits(&s, off, total)) return MESH_R_BAD_OP;
        uint32_t hi = lo + MESH_GPT2X_CHUNK < total ? lo + MESH_GPT2X_CHUNK : total;
        mesh_view v = mesh_ctx_view(ctx);
        if (op == MESH_GPT2X_OP_PUT) {
            MESH_CTX_FOR_STRIDED(ctx, hi - lo, i) gx_wr(&s, off + lo + i, mesh_view_f32(v, MESH_GPT2X_X_WINDOW + 4 * (lo + i)));
        } else {
            MESH_CTX_FOR_STRIDED(ctx, hi - lo, i) mesh_view_set_f32(v, MESH_GPT2X_X_WINDOW + 4 * (lo + i), gx_rd(&s, off + lo + i));
        }
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 3, 1);
        ok = true;
        break;
    }
    case MESH_GPT2X_OP_EMBED: {
        uint32_t tt = p[MESH_GPT2X_P_T], p0 = p[MESH_GPT2X_P_P0], wte = p[MESH_GPT2X_P_WTE], wpe = p[MESH_GPT2X_P_WPE], x = p[MESH_GPT2X_P_X];
        if (arg >= tt || p0 + tt > GX_CTX || !gx_fits(&s, wte, GX_VOCAB * GX_WIDTH) || !gx_fits(&s, wpe, GX_CTX * GX_WIDTH) || !gx_fits(&s, x, tt * GX_WIDTH))
            return MESH_R_BAD_OP;
        uint32_t tok = mesh_view_u32(mesh_ctx_view(ctx), MESH_GPT2X_X_TOKENS + 4 * arg);
        if (tok >= GX_VOCAB) return MESH_R_BAD_OP;
        MESH_CTX_FOR_STRIDED(ctx, GX_WIDTH, i)
        gx_wr(&s, x + arg * GX_WIDTH + i, gx_rd(&s, wte + tok * GX_WIDTH + i) + gx_rd(&s, wpe + (p0 + arg) * GX_WIDTH + i));
        ok = true;
        break;
    }
    case MESH_GPT2X_OP_NORM: {
        uint32_t src = p[MESH_GPT2X_P_A], g = p[MESH_GPT2X_P_B], b = p[MESH_GPT2X_P_BIAS], dst = p[MESH_GPT2X_P_C], tt = p[MESH_GPT2X_P_T];
        if (arg >= tt || !gx_fits(&s, src, tt * GX_WIDTH) || !gx_fits(&s, dst, tt * GX_WIDTH) || !gx_fits(&s, g, GX_WIDTH) || !gx_fits(&s, b, GX_WIDTH))
            return MESH_R_BAD_OP;
        gx_normalize(ctx, &s, src + arg * GX_WIDTH, dst + arg * GX_WIDTH, g, b);
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 1, 1);
        ok = true;
        break;
    }
    case MESH_GPT2X_OP_GEMM:
    case MESH_GPT2X_OP_GEMV: {
        uint32_t a = p[MESH_GPT2X_P_A], b = p[MESH_GPT2X_P_B], bias = p[MESH_GPT2X_P_BIAS], c = p[MESH_GPT2X_P_C];
        uint32_t m = p[MESH_GPT2X_P_M], k = p[MESH_GPT2X_P_K], n = p[MESH_GPT2X_P_N], epi = p[MESH_GPT2X_P_EPI];
        if (m == 0 || k == 0 || n == 0 || !gx_fits(&s, a, m * k) || !gx_fits(&s, b, k * n) || !gx_fits(&s, bias, n) || !gx_fits(&s, c, m * n) ||
            epi > MESH_GPT2X_EPI_BIAS_RESIDUAL)
            return MESH_R_BAD_OP;
        gx_finish f = {&s, epi, bias, c, n, 0};
        if (op == MESH_GPT2X_OP_GEMM) {
            mesh_shared sa, sb;
            if (!mesh_ctx_shared(ctx, GX_SH_OP, MESH_LINALG_TILE_SHARED / 2, 4, &sa) ||
                !mesh_ctx_shared(ctx, GX_SH_OP + 4 * (MESH_LINALG_TILE_SHARED / 2), MESH_LINALG_TILE_SHARED / 2, 4, &sb))
                return MESH_R_BAD_OP;
            if (!mesh_linalg_gemm_tile(ctx, s.p + a, s.p + b, m, k, n, arg, sa, sb, gx_finish2, &f)) return MESH_R_BAD_OP;
            if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 2, 1);
        } else {
            uint32_t nb = gx_div_ceil(n, MESH_GPT2X_GEMV_COLS);
            uint32_t r = arg / nb, cb = arg % nb;
            if (r >= m) return MESH_R_BAD_OP;
            mesh_shared sb, sx;
            if (!mesh_ctx_shared(ctx, GX_SH_OP, MESH_LINALG_GEMV_SLAB * MESH_LINALG_GEMV_COLS, 4, &sb) ||
                !mesh_ctx_shared(ctx, GX_SH_OP + 4 * MESH_LINALG_GEMV_SLAB * MESH_LINALG_GEMV_COLS, MESH_LINALG_GEMV_SLAB, 4, &sx))
                return MESH_R_BAD_OP;
            f.row = r;
            mesh_linalg_gemv_cols(ctx, s.p + a, s.p + b, k, n, r, cb * MESH_GPT2X_GEMV_COLS, sb, sx, gx_finish1, &f);
        }
        ok = true;
        break;
    }
    case MESH_GPT2X_OP_KV: {
        uint32_t tt = p[MESH_GPT2X_P_T], p0 = p[MESH_GPT2X_P_P0], qkv = p[MESH_GPT2X_P_QKV], kc = p[MESH_GPT2X_P_KC], vc = p[MESH_GPT2X_P_VC];
        if (arg >= tt || p0 + tt > GX_CTX || !gx_fits(&s, qkv, tt * 3 * GX_WIDTH) || !gx_fits(&s, kc, GX_CTX * GX_WIDTH) || !gx_fits(&s, vc, GX_CTX * GX_WIDTH))
            return MESH_R_BAD_OP;
        uint32_t src = qkv + arg * 3 * GX_WIDTH, dst = (p0 + arg) * GX_WIDTH;
        MESH_CTX_FOR_STRIDED(ctx, GX_WIDTH, i) {
            gx_wr(&s, kc + dst + i, gx_rd(&s, src + GX_WIDTH + i));
            gx_wr(&s, vc + dst + i, gx_rd(&s, src + 2 * GX_WIDTH + i));
        }
        ok = true;
        break;
    }
    case MESH_GPT2X_OP_ATTN: {
        uint32_t tt = p[MESH_GPT2X_P_T], p0 = p[MESH_GPT2X_P_P0], qb = p[MESH_GPT2X_P_QB];
        if (qb == 0 || p0 + tt > GX_CTX || !gx_fits(&s, p[MESH_GPT2X_P_QKV], tt * 3 * GX_WIDTH) || !gx_fits(&s, p[MESH_GPT2X_P_KC], GX_CTX * GX_WIDTH) ||
            !gx_fits(&s, p[MESH_GPT2X_P_VC], GX_CTX * GX_WIDTH) || !gx_fits(&s, p[MESH_GPT2X_P_Y], tt * GX_WIDTH))
            return MESH_R_BAD_OP;
        uint32_t nq = gx_div_ceil(tt, qb);
        uint32_t h = arg / nq, blk = arg % nq;
        if (h >= GX_HEADS) return MESH_R_BAD_OP;
        mesh_shared sq, ss, spart;
        if (!mesh_ctx_shared(ctx, GX_SH_Q, GX_HEAD_DIM, 4, &sq) || !mesh_ctx_shared(ctx, GX_SH_S, GX_CTX, 4, &ss) ||
            !mesh_ctx_shared(ctx, GX_SH_PART, MESH_LINALG_TILE_THREADS, 4, &spart))
            return MESH_R_BAD_OP;
        for (uint32_t r = blk * qb; r < tt && r < (blk + 1) * qb; r++) gx_attend(ctx, &s, p, h, r, p0 + r, sq, ss, spart);
        ok = true;
        break;
    }
    case MESH_GPT2X_OP_LOGITS: {
        uint32_t hh = p[MESH_GPT2X_P_A], wte = p[MESH_GPT2X_P_B], out = p[MESH_GPT2X_P_C];
        if (!gx_fits(&s, hh, GX_WIDTH) || !gx_fits(&s, wte, GX_VOCAB * GX_WIDTH) || !gx_fits(&s, out, GX_VOCAB) || arg * MESH_GPT2X_LOGIT_ROWS >= GX_VOCAB)
            return MESH_R_BAD_OP;
        /* Each warp: 32 rows, 4 at a time (more loads in flight; each row's order
         * is unchanged: lane l sums k = l, l + 32, .., then the butterfly). */
        uint32_t l = mesh_ctx_lane(ctx);
        uint32_t base = arg * MESH_GPT2X_LOGIT_ROWS + 32 * mesh_ctx_warp(ctx);
        for (uint32_t q = 0; q < 8; q++) {
            uint32_t r0 = base + 4 * q;
            if (r0 >= GX_VOCAB) break;
            /* Lane l's k = l, l + 32, .. (24 of them) in rounds of 4: 16 loads in flight, the adds in k order. */
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            uint32_t rows[4];
            for (uint32_t i = 0; i < 4; i++) rows[i] = r0 + i < GX_VOCAB - 1 ? r0 + i : GX_VOCAB - 1;
            for (uint32_t k = l; k < GX_WIDTH; k += 128) {
                float hk[4], w[4][4];
                for (uint32_t j = 0; j < 4; j++) hk[j] = gx_rd(&s, hh + k + 32 * j);
                for (uint32_t i = 0; i < 4; i++)
                    for (uint32_t j = 0; j < 4; j++) w[i][j] = gx_rd(&s, wte + rows[i] * GX_WIDTH + k + 32 * j);
                for (uint32_t j = 0; j < 4; j++)
                    for (uint32_t i = 0; i < 4; i++) acc[i] = acc[i] + w[i][j] * hk[j];
            }
            for (uint32_t i = 0; i < 4; i++) {
                float v = mesh_rt_warp_reduce_f32(acc[i], mesh_rt_add_f32);
                if (l == 0 && r0 + i < GX_VOCAB) gx_wr(&s, out + r0 + i, v);
            }
        }
        ok = true;
        break;
    }
    default: ok = false; break;
    }
    if (!ok) return MESH_R_BAD_OP;
    if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, MESH_GPT2X_C_TASKS, 1);
    return MESH_R_NONE;
}

MESH_PROGRAM(gx_init, gx_task);
