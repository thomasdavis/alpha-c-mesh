/* blocks: the block collectives of a multi-warp node, for kcheck (program.toml).
 * Every thread computes the same collectives in the same order; thread 0
 * writes the node-wide results, every thread its own scans and shared word.
 * Ported from alpha-rust-mesh programs/blocks/src/lib.rs. */
#include "mesh/rt.h"

#define BLOCKS_MAGIC 0x4b434c42u /* "BLCK" */
#define BLOCKS_V_MAGIC 0x0u
#define BLOCKS_V_TASKS 0x4u
#define BLOCKS_R0 0x1000u
#define BLOCKS_REGION 0x10000u
#define BLOCKS_HDR 0x40u
#define BLOCKS_PER 48u
#define BLOCKS_OP_EARLY 2u

/* mesh_ref::block::mix (lowbias32). */
static uint32_t blocks_mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

/* mesh_ref::block::test_values. */
static void blocks_test_values(uint32_t seed, uint32_t t, float *f, uint32_t *u, uint64_t *l) {
    uint32_t h = blocks_mix((seed * 0x9e3779b9u) ^ t);
    uint32_t h2 = blocks_mix(h ^ 0x85ebca6bu);
    uint32_t h3 = blocks_mix(h2 ^ 0xc2b2ae35u);
    uint32_t bits;
    switch (h & 15u) {
    case 0: bits = (h & 16u) ? 0x80000000u : 0u; break;
    case 1: bits = (h & 0x80000000u) | (h2 & 0x007fffffu) | 1u; break;
    case 2: bits = (h & 0x80000000u) | (200u << 23) | (h2 & 0x007fffffu); break;
    default: bits = (h & 0x80000000u) | ((112u + (h2 >> 28)) << 23) | (h3 & 0x007fffffu); break;
    }
    *f = mesh_f32_from_bits(bits);
    *u = h2;
    *l = (uint64_t)h3 << 32 | h;
}

static void blocks_init(const mesh_ctx *ctx) {
    if (mesh_ctx_leader(ctx)) mesh_ctx_set_u32(ctx, BLOCKS_V_MAGIC, BLOCKS_MAGIC);
}

static uint32_t blocks_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    if (op == MESH_OP_INIT) {
        blocks_init(ctx);
        return MESH_R_NONE;
    }
    if (op == MESH_OP_TASK) {
        uint32_t t = mesh_ctx_thread(ctx), n = mesh_ctx_threads(ctx);
        float f;
        uint32_t u;
        uint64_t l;
        blocks_test_values(arg, t, &f, &u, &l);
        float sf = mesh_ctx_block_sum_f32(ctx, f), mf = mesh_ctx_block_max_f32(ctx, f);
        uint32_t su = mesh_ctx_block_sum_u32(ctx, u), mu = mesh_ctx_block_max_u32(ctx, u);
        uint64_t sl = mesh_ctx_block_sum_u64(ctx, l), ml = mesh_ctx_block_max_u64(ctx, l);
        float fi = mesh_ctx_block_scan_f32(ctx, f, true), fe = mesh_ctx_block_scan_f32(ctx, f, false);
        uint32_t ui = mesh_ctx_block_scan_u32(ctx, u, true), ue = mesh_ctx_block_scan_u32(ctx, u, false);
        uint64_t li = mesh_ctx_block_scan_u64(ctx, l, true), le = mesh_ctx_block_scan_u64(ctx, l, false);
        uint32_t b = mesh_ctx_block_broadcast_u32(ctx, u, arg % n);
        /* Shared memory: every thread's f32 bits in, the mirrored thread's out. */
        uint32_t back = 0;
        mesh_shared sh;
        if (mesh_ctx_shared(ctx, 0, n, 4, &sh)) {
            mesh_shared_set_u32(sh, t, mesh_f32_bits(f));
            mesh_ctx_block_barrier(ctx);
            back = mesh_shared_get_u32(sh, n - 1 - t);
        }
        uint32_t r = BLOCKS_R0 + BLOCKS_REGION * (mesh_ctx_ticket(ctx) % 4);
        uint32_t p = r + BLOCKS_HDR + BLOCKS_PER * t;
        uint32_t mine[9] = {mesh_f32_bits(fi), mesh_f32_bits(fe), ui, ue, (uint32_t)li, (uint32_t)(li >> 32),
                            (uint32_t)le, (uint32_t)(le >> 32), back};
        for (uint32_t i = 0; i < 9; i++) mesh_view_set_u32(v, p + 4 * i, mine[i]);
        if (mesh_ctx_leader(ctx)) {
            uint32_t hdr[12] = {n, mesh_f32_bits(sf), mesh_f32_bits(mf), su, mu, (uint32_t)sl, (uint32_t)(sl >> 32),
                                (uint32_t)ml, (uint32_t)(ml >> 32), b, mesh_ctx_node(ctx), mesh_ctx_warps(ctx)};
            for (uint32_t i = 0; i < 12; i++) mesh_view_set_u32(v, r + 4 * i, hdr[i]);
            mesh_view_set_u32(v, BLOCKS_V_TASKS, mesh_view_u32(v, BLOCKS_V_TASKS) + 1);
            mesh_ctx_count(ctx, 0, 1);
        }
        return su;
    }
    if (op == BLOCKS_OP_EARLY) {
        /* Warps 1.. leave the task at once; warp 0 sums its lanes (a warp collective only). */
        if (mesh_ctx_warp(ctx) != 0) return 0;
        float s = mesh_rt_warp_sum((float)mesh_ctx_lane(ctx));
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 1, 1);
        return (uint32_t)s;
    }
    return MESH_R_BAD_OP;
}

MESH_PROGRAM(blocks_init, blocks_task);
