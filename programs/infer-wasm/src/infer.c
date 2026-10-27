/* infer for the wasm executor (alpha-rust-mesh programs/infer/wasm): the trained cubic over a
 * 64-point grid, reading train's params through a peer view (manifest reads = ["train"]), in the
 * cluster float mode (Z): the same bits as the GPUs' image. Layout: mesh-abi views::infer. */
#include "mesh/abi/view_infer.h"
#include "mesh/abi/view_train.h"
#include "mesh/ref/cubic.h"
#include "mesh/rt_wasm.h"

static float xs[MESH_INFER_POINTS], ys[MESH_INFER_POINTS];

/* The grid: x_i = -1 + 2 i / (POINTS - 1) (mesh-ref's, operation for operation the GPU's). */
static void init(const mesh_ctx *ctx) {
    mesh_cubic_grid(MESH_INFER_POINTS, xs);
    mesh_view_write_f32s(mesh_ctx_view(ctx), MESH_INFER_I_X, xs, MESH_INFER_POINTS);
}

/* Returns POINTS, or R_NO_TRAIN when no train is loaded. */
static uint32_t run(const mesh_ctx *ctx) {
    uint32_t s;
    mesh_view tv;
    if (!mesh_ctx_find(ctx, "train", 5, &s) || !mesh_ctx_peer_view(ctx, s, &tv)) return MESH_INFER_R_NO_TRAIN;
    float p[4];
    mesh_view_read_f32s(tv, MESH_TRAIN_T_PARAMS, p, 4);
    mesh_view v = mesh_ctx_view(ctx);
    mesh_view_read_f32s(v, MESH_INFER_I_X, xs, MESH_INFER_POINTS);
    for (uint32_t i = 0; i < MESH_INFER_POINTS; i++) ys[i] = mesh_cubic(p, xs[i]);
    mesh_view_write_f32s(v, MESH_INFER_I_Y, ys, MESH_INFER_POINTS);
    mesh_view_write_f32s(v, MESH_INFER_I_PARAMS, p, 4);
    mesh_view_set_u32(v, MESH_INFER_I_RUNS, mesh_view_u32(v, MESH_INFER_I_RUNS) + 1u);
    mesh_view_set_u32(v, MESH_INFER_I_TRAIN_STEP, mesh_view_u32(tv, MESH_TRAIN_T_NEXT_STEP));
    mesh_ctx_count(ctx, 0, 1);
    return MESH_INFER_POINTS;
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    switch (op) {
    case MESH_OP_TASK: return run(ctx);
    case MESH_INFER_OP_PROBE_READ: {
        mesh_view pv;
        return mesh_ctx_peer_view(ctx, arg, &pv) ? mesh_view_len(pv) : MESH_INFER_R_NO_VIEW;
    }
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
