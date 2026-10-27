/* tick: the scheduler's test program (program.toml has the layout). A
 * migration must carry its view: the ticks go on from where they were, the
 * inits stay 1 (no OP_INIT on the target), and `task tick 3 0` finds every
 * sampled pattern word in place.
 * Ported from alpha-rust-mesh programs/tick/src/lib.rs. */
#include "mesh/rt.h"

#define TICK_MAGIC 0x4b434954u /* "TICK" */
#define TICK_TICKS 0x4u
#define TICK_INITS 0x8u
#define TICK_MISMATCH 0xcu
#define TICK_REGION 0x40u
#define TICK_OP_VERIFY 3u
#define TICK_SAMPLES 4096u
#define TICK_RECENT 64u

static uint32_t tick_pattern(uint32_t i) { return (i * 2654435761u) ^ 0x5a5a5a5au; }

/* Word i of the region after `ticks` ticks (w words). */
static uint32_t tick_expect(uint32_t i, uint32_t ticks, uint32_t w) {
    if (ticks <= i) return tick_pattern(i);
    /* The last tick t < ticks with t mod w == i. */
    uint32_t t = i + (ticks - 1 - i) / w * w;
    return tick_pattern(i) ^ (t + 1);
}

static uint32_t tick_words(const mesh_ctx *ctx) { return (mesh_ctx_view_len(ctx) - TICK_REGION) / 4; }

static void tick_init(const mesh_ctx *ctx) {
    uint32_t w = tick_words(ctx);
    for (uint32_t i = mesh_ctx_lane(ctx); i < w; i += 32) mesh_ctx_set_u32(ctx, TICK_REGION + 4 * i, tick_pattern(i));
    if (mesh_ctx_leader(ctx)) {
        mesh_ctx_set_u32(ctx, 0x0, TICK_MAGIC);
        mesh_ctx_set_u32(ctx, TICK_TICKS, 0);
        mesh_ctx_set_u32(ctx, TICK_INITS, mesh_ctx_view_u32(ctx, TICK_INITS) + 1);
        mesh_ctx_count(ctx, 1, 1);
    }
}

static uint32_t tick_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    (void)arg;
    uint32_t w = tick_words(ctx);
    if (op == MESH_OP_TASK) {
        if (mesh_ctx_leader(ctx)) {
            uint32_t t = mesh_ctx_view_u32(ctx, TICK_TICKS);
            uint32_t i = t % w;
            mesh_ctx_set_u32(ctx, TICK_REGION + 4 * i, tick_pattern(i) ^ (t + 1));
            mesh_ctx_set_u32(ctx, TICK_TICKS, t + 1);
            mesh_ctx_count(ctx, 0, 1);
        }
        return 0;
    }
    if (op == TICK_OP_VERIFY) {
        uint32_t bad = 0;
        if (mesh_ctx_leader(ctx)) {
            uint32_t t = mesh_ctx_view_u32(ctx, TICK_TICKS);
            for (uint32_t s = 0; s < TICK_SAMPLES; s++) {
                uint32_t i = (uint32_t)((uint64_t)s * w / TICK_SAMPLES);
                if (mesh_ctx_view_u32(ctx, TICK_REGION + 4 * i) != tick_expect(i, t, w)) bad++;
            }
            uint32_t recent = TICK_RECENT < t ? TICK_RECENT : t;
            for (uint32_t k = 1; k <= recent; k++) {
                uint32_t i = (t - k) % w;
                if (mesh_ctx_view_u32(ctx, TICK_REGION + 4 * i) != tick_expect(i, t, w)) bad++;
            }
            mesh_ctx_set_u32(ctx, TICK_MISMATCH, bad);
        }
        return bad;
    }
    return MESH_R_BAD_OP;
}

MESH_PROGRAM(tick_init, tick_task);
