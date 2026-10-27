/* pipesrc-n: the pipe demo's source as a native worker (programs/pipesrc's
 * twin, view pipedemo). Each continuous task pushes up to S_BATCH records:
 * msgs into pipehash's inbox (mode 0), or digests it hashed itself into a sink
 * (mode 1). A full pipe ends the task (backpressure); no consumer counts as gone.
 * Ported from alpha-rust-mesh programs/pipesrc-n/src/main.rs. */
#include "pipesrc.h"

#include "mesh/abi/ops.h"
#include "mesh/abi/pipe.h"
#include "mesh/abi/view_pipedemo.h"

#include <time.h>

void pipesrc_init(const mesh_worker_ctx *ctx, void *user) {
    (void)user;
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_MODE, MESH_PIPEDEMO_MODE_GPU);
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_BATCH, MESH_PIPEDEMO_SRC_BATCH);
}

static bool dst(const mesh_worker_ctx *ctx, uint32_t mode, uint32_t *d) {
    if (mode == MESH_PIPEDEMO_MODE_GPU) return mesh_ctx_find(ctx, "pipehash", d);
    if (mode == MESH_PIPEDEMO_MODE_DIRECT) return mesh_ctx_find(ctx, "pipesink", d) || mesh_ctx_find(ctx, "pipesink-w", d);
    return false;
}

static void le_bytes(const uint32_t *w, size_t n, uint8_t *out) {
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) out[4 * i + k] = (uint8_t)(w[i] >> (8 * k));
}

static void gone(const mesh_worker_ctx *ctx) {
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_GONE, mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_GONE) + 1);
    mesh_ctx_count(ctx, 2, 1);
}

static uint32_t send(const mesh_worker_ctx *ctx) {
    uint32_t mode = mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_MODE);
    if (mode == MESH_PIPEDEMO_MODE_OFF) {
        struct timespec ts = {0, 200000};
        nanosleep(&ts, NULL);
        return 0;
    }
    uint64_t iv = (uint64_t)mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_INTERVAL_US) * 1000;
    uint64_t now = mesh_ctx_host_ns(ctx);
    /* Paced: wait out the interval in the task (bounded), so the pace is not the driver's tick. */
    uint64_t last = mesh_ctx_view_u64(ctx, MESH_PIPEDEMO_S_LAST_NS);
    if (iv > 0 && now - last < iv) {
        if (iv - (now - last) > 2000000) return 0;
        while (now - last < iv) now = mesh_ctx_host_ns(ctx);
    }
    mesh_ctx_set_u64(ctx, MESH_PIPEDEMO_S_LAST_NS, now);
    uint32_t d;
    if (!dst(ctx, mode, &d)) {
        gone(ctx);
        return 0;
    }
    mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_S_DST, d + 1);
    uint32_t seq = mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_NEXT_SEQ), sent = 0, full = 0, batch = mesh_ctx_view_u32(ctx, MESH_PIPEDEMO_S_BATCH);
    for (uint32_t i = 0; i < batch; i++) {
        uint32_t m[16], dg[12];
        uint8_t rec[64];
        size_t n;
        mesh_pipedemo_msg(seq, mesh_ctx_host_ns(ctx), MESH_PIPEDEMO_ORIGIN_NATIVE, m);
        if (mode == MESH_PIPEDEMO_MODE_GPU) {
            le_bytes(m, 16, rec);
            n = 64;
        } else {
            mesh_pipedemo_digest(m, 0, dg);
            le_bytes(dg, 12, rec);
            n = 48;
        }
        int32_t code = mesh_ctx_push_code(ctx, d, rec, n);
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

uint32_t pipesrc_task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user) {
    (void)user;
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
