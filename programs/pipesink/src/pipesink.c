/* pipesink: the pipe demo's sink as a native worker (view pipedemo, K_*). Each
 * continuous task drains up to DRAIN digests from its inbox; each is checked
 * against mesh-ref's SHA-256d of the msg rebuilt from its seq (an
 * implementation independent of the hashing stage's), and its latency -- the
 * host clock now minus the source's stamp: one clock for every executor --
 * goes into a 1 us histogram in the view.
 * Ported from alpha-rust-mesh programs/pipesink/src/main.rs. */
#include "pipesink.h"

#include "hist_quantile.h"
#include "mesh/abi/ops.h"
#include "mesh/abi/view_pipedemo.h"
#include "mesh/ref/sha256d.h"

#include <string.h>

#define DRAIN 512u
/* An idle task waits this long (ns) for a record. */
#define WAIT_NS UINT64_C(1000000)

static void check(const mesh_worker_ctx *ctx, const uint8_t rec[48], uint64_t now) {
    uint32_t w[12];
    for (int i = 0; i < 12; i++) w[i] = (uint32_t)rec[4 * i] | (uint32_t)rec[4 * i + 1] << 8 | (uint32_t)rec[4 * i + 2] << 16 | (uint32_t)rec[4 * i + 3] << 24;
    uint64_t stamp = w[MESH_PIPEDEMO_R_STAMP_LO] | (uint64_t)w[MESH_PIPEDEMO_R_STAMP_HI] << 32;
    uint32_t m[16];
    mesh_pipedemo_msg(w[MESH_PIPEDEMO_R_SEQ], stamp, w[MESH_PIPEDEMO_R_ORIGIN] & 0xff, m);
    uint8_t bytes[64], want[32], got[32];
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 4; k++) bytes[4 * i + k] = (uint8_t)(m[i] >> (8 * k));
    mesh_sha256d(bytes, sizeof bytes, want);
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 4; k++) got[4 * i + k] = (uint8_t)(w[MESH_PIPEDEMO_R_PAYLOAD + i] >> (24 - 8 * k));
    mesh_view v = mesh_ctx_view(ctx);
    mesh_view_add_u32(v, MESH_PIPEDEMO_K_RECORDS, 1);
    if (memcmp(got, want, 32) != 0) {
        mesh_view_add_u32(v, MESH_PIPEDEMO_K_BAD, 1);
        mesh_ctx_count(ctx, 1, 1);
        mesh_view_write(v, MESH_PIPEDEMO_K_LAST_BAD, rec, 48);
    }
    if (w[MESH_PIPEDEMO_R_ORIGIN] & MESH_PIPEDEMO_PATH_GPU) {
        mesh_view_add_u32(v, MESH_PIPEDEMO_K_GPU, 1);
        mesh_ctx_count(ctx, 2, 1);
    } else {
        mesh_view_add_u32(v, MESH_PIPEDEMO_K_CPU, 1);
        mesh_ctx_count(ctx, 3, 1);
    }
    uint64_t d = now > stamp ? (now - stamp) / 1000 : 0;
    uint32_t us = d > UINT32_MAX ? UINT32_MAX : (uint32_t)d;
    mesh_view_add_u32(v, MESH_PIPEDEMO_K_HIST + 4 * (us < MESH_PIPEDEMO_K_BINS - 1 ? us : MESH_PIPEDEMO_K_BINS - 1), 1);
    if (us > mesh_view_u32(v, MESH_PIPEDEMO_K_MAX_US)) mesh_view_set_u32(v, MESH_PIPEDEMO_K_MAX_US, us);
}

static void stats(const mesh_worker_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    static _Thread_local uint32_t hist[MESH_PIPEDEMO_K_BINS];
    for (uint32_t i = 0; i < MESH_PIPEDEMO_K_BINS; i++) hist[i] = mesh_view_u32(v, MESH_PIPEDEMO_K_HIST + 4 * i);
    const uint32_t q[3][2] = {{MESH_PIPEDEMO_K_P50_US, 500}, {MESH_PIPEDEMO_K_P90_US, 900}, {MESH_PIPEDEMO_K_P99_US, 990}};
    for (int i = 0; i < 3; i++) mesh_view_set_u32(v, q[i][0], hist_quantile(hist, MESH_PIPEDEMO_K_BINS, q[i][1]));
}

static uint32_t drain(const mesh_worker_ctx *ctx) {
    uint8_t rec[48];
    uint32_t n = 0, len = 0;
    /* An empty inbox: wait for a record (bounded, past the driver's tick) before the task ends. */
    uint64_t t0 = mesh_ctx_host_ns(ctx);
    while (!mesh_ctx_pipe_len(ctx, mesh_ctx_slot(ctx), &len) || len == 0) {
        if (mesh_ctx_host_ns(ctx) - t0 > WAIT_NS) {
            mesh_view_add_u32(mesh_ctx_view(ctx), MESH_PIPEDEMO_K_EMPTY, 1);
            return 0;
        }
    }
    while (n < DRAIN && mesh_ctx_pop(ctx, rec, sizeof rec)) {
        check(ctx, rec, mesh_ctx_host_ns(ctx));
        n++;
    }
    mesh_ctx_count(ctx, 0, n);
    if (mesh_ctx_ticket(ctx) % MESH_PIPEDEMO_K_STATS_EVERY == 0) stats(ctx);
    return n;
}

uint32_t pipesink_task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user) {
    (void)arg, (void)user;
    switch (op) {
    case MESH_OP_TASK: return drain(ctx);
    case MESH_PIPEDEMO_OP_STATS: stats(ctx); return 0;
    case MESH_PIPEDEMO_OP_SINK_RESET: {
        mesh_view v = mesh_ctx_view(ctx);
        for (uint32_t o = 0; o < MESH_PIPEDEMO_K_HIST; o += 4) mesh_view_set_u32(v, o, 0);
        for (uint32_t i = 0; i < MESH_PIPEDEMO_K_BINS; i++) mesh_view_set_u32(v, MESH_PIPEDEMO_K_HIST + 4 * i, 0);
        return 0;
    }
    default: return MESH_R_BAD_OP;
    }
}

void pipesink_init(const mesh_worker_ctx *ctx, void *user) { (void)ctx, (void)user; }
