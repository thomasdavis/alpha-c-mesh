/* matmul: the exact-mode GPU GEMM and GEMV (program.toml; the tile kernel is
 * mesh_linalg_gemm_tile, shared with programs/gpt2x). A node is 8 warps (256
 * threads): a 64 x 64 tile of C, each thread 4 x 4 outputs (rows ty + 16 i,
 * columns tx + 16 j), the k dimension in slabs of 16 through shared memory.
 * Every output is summed sequentially in k from +0.0 -- a slab only splits the
 * loop, never reorders it -- with a multiply and an add, both correctly rounded
 * and FTZ (the build refuses a fused op).
 * Ported from alpha-rust-mesh programs/matmul/src/lib.rs. */
#include "mesh/rt.h"
#include "mesh/rt/linalg.h"

#define MATMUL_MAGIC 0x4d544d4du /* "MMTM" */
#define MATMUL_V_MAGIC 0x0u
#define MATMUL_V_DIMS 0x4u /* M, K, N, seed A, seed B */
#define MATMUL_V_TILES 0x18u
#define MATMUL_V_WINDOW 0x1000u
#define MATMUL_CHUNK 65536u
#define MATMUL_MAXD 3072u
#define MATMUL_MAXE ((size_t)MATMUL_MAXD * MATMUL_MAXD)
#define MATMUL_OP_SET 3u
#define MATMUL_OP_GEN 4u
#define MATMUL_OP_MM 5u
#define MATMUL_OP_MV 6u
#define MATMUL_OP_COPY 7u
#define MATMUL_THREADS MESH_LINALG_TILE_THREADS
/* Scratch bytes the matrices need: A, B, C of up to MAXD x MAXD, then x and y. */
#define MATMUL_SCRATCH_MIN (4 * (3 * MATMUL_MAXE + 2 * 4096))

/* mesh_ref::block::mix. */
static uint32_t matmul_mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
/* mesh_ref::matmul::value. */
static float matmul_value(uint32_t seed, uint32_t i) {
    uint32_t h = matmul_mix(((seed * 0x9e3779b9u) + i) * 0x85ebca6bu ^ seed);
    return (float)(h >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

typedef struct {
    float *a, *b, *c, *x, *y;
} matmul_mats;

static bool matmul_get_mats(const mesh_ctx *ctx, matmul_mats *mt) {
    uint32_t len;
    uint8_t *p = mesh_ctx_scratch_ptr(ctx, &len);
    if ((size_t)len < MATMUL_SCRATCH_MIN) return false;
    float *a = (float *)(void *)p;
    *mt = (matmul_mats){a, a + MATMUL_MAXE, a + 2 * MATMUL_MAXE, a + 3 * MATMUL_MAXE, a + 3 * MATMUL_MAXE + 4096};
    return true;
}

static bool matmul_dims(mesh_view v, uint32_t *m, uint32_t *k, uint32_t *n) {
    *m = mesh_view_u32(v, MATMUL_V_DIMS);
    *k = mesh_view_u32(v, MATMUL_V_DIMS + 4);
    *n = mesh_view_u32(v, MATMUL_V_DIMS + 8);
    return *m >= 1 && *m <= MATMUL_MAXD && *k >= 1 && *k <= MATMUL_MAXD && *n >= 1 && *n <= MATMUL_MAXD;
}

static void matmul_init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    if (mesh_ctx_leader(ctx)) {
        const uint32_t d[5] = {768, 768, 768, 1, 2};
        for (uint32_t i = 0; i < 5; i++) mesh_view_set_u32(v, MATMUL_V_DIMS + 4 * i, d[i]);
        mesh_view_set_u32(v, MATMUL_V_MAGIC, MATMUL_MAGIC);
    }
}

typedef struct {
    float *c;
    uint32_t n;
} matmul_store;
static void matmul_store_c(uint32_t r, uint32_t col, float v, void *user) {
    matmul_store *s = user;
    s->c[(size_t)r * s->n + col] = v;
}

static uint32_t matmul_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    if (op == MESH_OP_INIT) {
        matmul_init(ctx);
        return MESH_R_NONE;
    }
    if (op == MATMUL_OP_SET) {
        if (arg >> 24 > 4) return MESH_R_BAD_OP;
        if (mesh_ctx_leader(ctx)) mesh_view_set_u32(v, MATMUL_V_DIMS + 4 * (arg >> 24), arg & 0x00ffffffu);
        return MESH_R_NONE;
    }
    /* The rest needs the node's shape, the scratch and valid dimensions (the same answer on every thread). */
    matmul_mats mt;
    uint32_t m, k, n;
    if (!matmul_get_mats(ctx, &mt) || !matmul_dims(v, &m, &k, &n)) return MESH_R_BAD_OP;
    if (mesh_ctx_threads(ctx) != MATMUL_THREADS) return MESH_R_BAD_OP;
    bool ok;
    switch (op) {
    case MATMUL_OP_GEN: {
        uint32_t which = arg >> 24, chunk = arg & 0x00ffffffu, len, seed;
        float *dst;
        if (which == 0) { dst = mt.a; len = m * k; seed = mesh_view_u32(v, MATMUL_V_DIMS + 12); }
        else if (which == 1) { dst = mt.b; len = k * n; seed = mesh_view_u32(v, MATMUL_V_DIMS + 16); }
        else if (which == 2) { dst = mt.x; len = k; seed = mesh_view_u32(v, MATMUL_V_DIMS + 16) ^ 0x55555555u; }
        else return MESH_R_BAD_OP;
        uint32_t lo = chunk * MATMUL_CHUNK;
        if (lo >= len) return MESH_R_BAD_OP;
        uint32_t hi = lo + MATMUL_CHUNK < len ? lo + MATMUL_CHUNK : len;
        MESH_CTX_FOR_STRIDED(ctx, hi - lo, i) dst[lo + i] = matmul_value(seed, lo + i);
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 2, 1);
        ok = true;
        break;
    }
    case MATMUL_OP_MM: {
        mesh_shared sa, sb;
        if (!mesh_ctx_shared(ctx, 0, MESH_LINALG_BK * MESH_LINALG_TILE, 4, &sa) ||
            !mesh_ctx_shared(ctx, 4 * MESH_LINALG_BK * MESH_LINALG_TILE, MESH_LINALG_BK * MESH_LINALG_TILE, 4, &sb))
            return MESH_R_BAD_OP;
        matmul_store st = {mt.c, n};
        ok = mesh_linalg_gemm_tile(ctx, mt.a, mt.b, m, k, n, arg, sa, sb, matmul_store_c, &st);
        if (ok && mesh_ctx_leader(ctx)) {
            mesh_ctx_count(ctx, 0, 1);
            mesh_view w;
            if (mesh_view_sub(v, MATMUL_V_TILES, 4, &w)) mesh_rt_atom_add(mesh_view_ptr(w), 1);
        }
        break;
    }
    case MATMUL_OP_MV: {
        /* Rows arg x 64 ..: warp w takes rows + 8 w .. + 8, one row at a time. */
        uint32_t r0 = arg * MESH_LINALG_TILE;
        if (r0 >= m) return MESH_R_BAD_OP;
        uint32_t l = mesh_ctx_lane(ctx);
        for (uint32_t rr = 0; rr < 8; rr++) {
            uint32_t r = r0 + 8 * mesh_ctx_warp(ctx) + rr;
            if (r >= m) break;
            float s = 0.0f;
            for (uint32_t kk = l; kk < k; kk += 32) s = s + mt.a[(size_t)r * k + kk] * mt.x[kk];
            s = mesh_rt_warp_reduce_f32(s, mesh_rt_add_f32);
            if (l == 0) mt.y[r] = s;
        }
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 1, 1);
        ok = true;
        break;
    }
    case MATMUL_OP_COPY: {
        uint32_t which = arg >> 24, chunk = arg & 0x00ffffffu, len;
        const float *src;
        if (which == 0) { src = mt.c; len = m * n; }
        else if (which == 1) { src = mt.y; len = m; }
        else return MESH_R_BAD_OP;
        uint32_t lo = chunk * MATMUL_CHUNK;
        if (lo >= len) return MESH_R_BAD_OP;
        uint32_t hi = lo + MATMUL_CHUNK < len ? lo + MATMUL_CHUNK : len;
        MESH_CTX_FOR_STRIDED(ctx, hi - lo, i) mesh_view_set_f32(v, MATMUL_V_WINDOW + 4 * i, src[lo + i]);
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 3, 1);
        ok = true;
        break;
    }
    default: ok = false; break;
    }
    return ok ? MESH_R_NONE : MESH_R_BAD_OP;
}

MESH_PROGRAM(matmul_init, matmul_task);
