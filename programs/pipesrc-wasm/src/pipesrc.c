/* pipesrc: the pipe demo's source on the wasm executor (alpha-rust-mesh programs/pipesrc/wasm;
 * mesh-abi views::pipedemo). Each continuous task pushes up to S_BATCH records: msgs into
 * pipehash's inbox (mode 0, the GPU hashes them), or digests it hashed itself straight into a sink
 * (mode 1). A full pipe ends the task (backpressure: nothing is dropped, the record is pushed by a
 * later task); no consumer loaded counts as gone. Every record carries the host clock at its push. */
#include "mesh/abi/view_pipedemo.h"
#include "mesh/rt_wasm.h"

static void init(const mesh_ctx *ctx) {
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_MODE, MESH_PIPEDEMO_MODE_GPU);
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_BATCH, MESH_PIPEDEMO_SRC_BATCH);
}

static bool dst(const mesh_ctx *ctx, uint32_t mode, uint32_t *slot) {
    switch (mode) {
    case MESH_PIPEDEMO_MODE_GPU: return mesh_ctx_find(ctx, "pipehash", 8, slot);
    case MESH_PIPEDEMO_MODE_DIRECT: return mesh_ctx_find(ctx, "pipesink", 8, slot) || mesh_ctx_find(ctx, "pipesink-w", 10, slot);
    default: return false;
    }
}

static void gone(const mesh_ctx *ctx) {
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_GONE, mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_GONE) + 1u);
    mesh_ctx_count(ctx, 2, 1);
}

static uint32_t send(const mesh_ctx *ctx) {
    uint32_t mode = mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_MODE);
    if (mode == MESH_PIPEDEMO_MODE_OFF) return 0;
    uint64_t iv = (uint64_t)mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_INTERVAL_US) * 1000u;
    /* Paced: wait out the interval in the task (bounded), so the pace is not the driver's tick. */
    uint64_t now = mesh_ctx_host_ns(ctx);
    uint64_t last = mesh_ctx_view_u64(ctx, MESH_PIPEDEMO_S_LAST_NS);
    if (iv > 0 && now - last < iv) {
        if (iv - (now - last) > 2000000u) return 0;
        while (now - last < iv) now = mesh_ctx_host_ns(ctx);
    }
    mesh_ctx_set_u64(ctx, MESH_PIPEDEMO_S_LAST_NS, now);
    uint32_t d;
    if (!dst(ctx, mode, &d)) {
        gone(ctx);
        return 0;
    }
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_DST, d + 1u);
    uint32_t seq = mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_NEXT_SEQ), sent = 0, full = 0;
    uint32_t batch = mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_BATCH);
    for (uint32_t i = 0; i < batch; i++) {
        uint32_t m[16];
        mesh_pipedemo_msg(seq, mesh_ctx_host_ns(ctx), MESH_PIPEDEMO_ORIGIN_WASM, m);
        int32_t code;
        if (mode == MESH_PIPEDEMO_MODE_GPU) {
            code = mesh_ctx_push_code(ctx, d, m, sizeof m);
        } else {
            uint32_t g[12];
            mesh_pipedemo_digest(m, 0, g);
            code = mesh_ctx_push_code(ctx, d, g, sizeof g);
        }
        if (code == MESH_PIPE_OK) {
            seq++;
            sent++;
        } else if (code == MESH_PIPE_NONE) {
            full = 1;
            break;
        } else {
            gone(ctx);
            break;
        }
    }
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_NEXT_SEQ, seq);
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_SENT, mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_SENT) + sent);
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_FULL, mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_FULL) + full);
    mesh_ctx_count(ctx, 0, sent);
    mesh_ctx_count(ctx, 1, full);
    return sent;
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    switch (op) {
    case MESH_OP_TASK: return send(ctx);
    case MESH_PIPEDEMO_OP_MODE:
        if (arg > MESH_PIPEDEMO_MODE_OFF) return MESH_R_BAD_OP;
        mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_MODE, arg);
        return 0;
    case MESH_PIPEDEMO_OP_BATCH:
        if (arg < 1 || arg > 4096) return MESH_R_BAD_OP;
        mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_BATCH, arg);
        return 0;
    case MESH_PIPEDEMO_OP_INTERVAL: mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_INTERVAL_US, arg); return 0;
    case MESH_PIPEDEMO_OP_RESET:
        mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_SENT, 0);
        mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_FULL, 0);
        mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_GONE, 0);
        return 0;
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
