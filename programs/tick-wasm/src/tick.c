/* tick for the wasm executor (alpha-rust-mesh programs/tick/wasm; the same layout and ops as
 * programs/tick). The view past REGION holds pattern(i) per word; each task ticks word t % words
 * to pattern(i) ^ (t + 1); OP_VERIFY samples the region against what the ticks must have left. */
#include "mesh/rt_wasm.h"

#define MAGIC 0x4b434954u /* "TICK" */
#define TICKS 0x4u
#define INITS 0x8u
#define MISMATCH 0xcu
#define REGION 0x40u
#define OP_VERIFY 3u
#define SAMPLES 4096u
#define RECENT 64u

static uint32_t pattern(uint32_t i) { return (i * 2654435761u) ^ 0x5a5a5a5au; }

static uint32_t expect(uint32_t i, uint32_t ticks, uint32_t w) {
    if (ticks <= i) return pattern(i);
    uint32_t t = i + (ticks - 1 - i) / w * w;
    return pattern(i) ^ (t + 1u);
}

static uint32_t words(const mesh_ctx *ctx) { return (mesh_view_len(mesh_ctx_view(ctx)) - REGION) / 4; }

static uint32_t buf[1024];

static void init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    uint32_t w = words(ctx);
    /* In 4 KiB writes (one host call each). */
    for (uint32_t i = 0; i < w;) {
        uint32_t n = w - i < 1024 ? w - i : 1024;
        for (uint32_t k = 0; k < n; k++) buf[k] = pattern(i + k);
        mesh_view_write_u32s(v, REGION + 4 * i, buf, n);
        i += n;
    }
    mesh_view_set_u32(v, 0x0, MAGIC);
    mesh_view_set_u32(v, TICKS, 0);
    mesh_view_set_u32(v, INITS, mesh_view_u32(v, INITS) + 1u);
    mesh_ctx_count(ctx, 1, 1);
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    (void)arg;
    mesh_view v = mesh_ctx_view(ctx);
    uint32_t w = words(ctx);
    switch (op) {
    case MESH_OP_TASK: {
        uint32_t t = mesh_view_u32(v, TICKS);
        uint32_t i = t % w;
        mesh_view_set_u32(v, REGION + 4 * i, pattern(i) ^ (t + 1u));
        mesh_view_set_u32(v, TICKS, t + 1u);
        mesh_ctx_count(ctx, 0, 1);
        return 0;
    }
    case OP_VERIFY: {
        uint32_t t = mesh_view_u32(v, TICKS);
        uint32_t bad = 0;
        for (uint32_t s = 0; s < SAMPLES; s++) {
            uint32_t i = (uint32_t)((uint64_t)s * w / SAMPLES);
            if (mesh_view_u32(v, REGION + 4 * i) != expect(i, t, w)) bad++;
        }
        uint32_t recent = RECENT < t ? RECENT : t;
        for (uint32_t k = 1; k <= recent; k++) {
            uint32_t i = (t - k) % w;
            if (mesh_view_u32(v, REGION + 4 * i) != expect(i, t, w)) bad++;
        }
        mesh_view_set_u32(v, MISMATCH, bad);
        return bad;
    }
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
