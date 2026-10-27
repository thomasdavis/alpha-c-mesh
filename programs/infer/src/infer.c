/* infer: the trained cubic over a 64-point grid. It reads train's params
 * through mesh_ctx_peer_view (manifest `reads = ["train"]`, so the runtime
 * refuses any other view). Layout: @ajax/mesh-abi-view-infer. The loops
 * mirror mesh-kcheck's CPU reference index by index (bitwise checks).
 * Ported from alpha-rust-mesh programs/infer/src/lib.rs. */
#include "mesh/rt.h"

static float infer_cubic(const float p[4], float x) {
    float x2 = x * x;
    float x3 = x2 * x;
    return p[0] + p[1] * x + p[2] * x2 + p[3] * x3;
}

/* The grid: x_i = -1 + 2 i / (POINTS - 1); lane l writes points l and l + 32. */
static void infer_init(const mesh_ctx *ctx) {
    uint32_t l = mesh_ctx_lane(ctx);
    uint32_t is[2] = {l, l + 32};
    for (int k = 0; k < 2; k++) {
        uint32_t i = is[k];
        mesh_ctx_set_f32(ctx, MESH_INFER_I_X + 4 * i, -1.0f + 2.0f * (float)i / (float)(MESH_INFER_POINTS - 1));
    }
}

/* Returns POINTS, or R_NO_TRAIN when no train is loaded. */
static uint32_t infer_run(const mesh_ctx *ctx) {
    uint32_t s;
    mesh_view tv;
    if (!mesh_ctx_find(ctx, "train", 5, &s) || !mesh_ctx_peer_view(ctx, s, &tv)) return MESH_INFER_R_NO_TRAIN;
    float p[4] = {mesh_view_f32(tv, MESH_TRAIN_T_PARAMS), mesh_view_f32(tv, MESH_TRAIN_T_PARAMS + 4),
                  mesh_view_f32(tv, MESH_TRAIN_T_PARAMS + 8), mesh_view_f32(tv, MESH_TRAIN_T_PARAMS + 12)};
    uint32_t l = mesh_ctx_lane(ctx);
    uint32_t is[2] = {l, l + 32};
    for (int k = 0; k < 2; k++) {
        uint32_t i = is[k];
        mesh_ctx_set_f32(ctx, MESH_INFER_I_Y + 4 * i, infer_cubic(p, mesh_ctx_view_f32(ctx, MESH_INFER_I_X + 4 * i)));
    }
    if (mesh_ctx_leader(ctx)) {
        for (uint32_t k = 0; k < 4; k++) mesh_ctx_set_f32(ctx, MESH_INFER_I_PARAMS + 4 * k, p[k]);
        mesh_ctx_set_u32(ctx, MESH_INFER_I_RUNS, mesh_ctx_view_u32(ctx, MESH_INFER_I_RUNS) + 1);
        mesh_ctx_set_u32(ctx, MESH_INFER_I_TRAIN_STEP, mesh_view_u32(tv, MESH_TRAIN_T_NEXT_STEP));
        mesh_ctx_count(ctx, 0, 1);
    }
    return MESH_INFER_POINTS;
}

static uint32_t infer_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    if (op == MESH_OP_TASK) return infer_run(ctx);
    if (op == MESH_INFER_OP_PROBE_READ) {
        mesh_view v;
        return mesh_ctx_peer_view(ctx, arg, &v) ? mesh_view_len(v) : MESH_INFER_R_NO_VIEW;
    }
    return MESH_R_BAD_OP;
}

MESH_PROGRAM(infer_init, infer_task);
