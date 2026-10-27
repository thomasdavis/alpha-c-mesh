/* pipesink-w: programs/pipesink on the wasm executor (alpha-rust-mesh programs/pipesink-w/wasm;
 * mesh-abi views::pipedemo, K_*). Drains up to DRAIN digests a task; checks each against mesh-ref's
 * SHA-256 of the msg rebuilt from its seq; histograms the latency since the source's stamp (the
 * host clock: the same for every executor). */
#include "hist_quantile.h"
#include "mesh/abi/view_pipedemo.h"
#include "mesh/ref/sha256d.h"
#include "mesh/rt_wasm.h"

#define DRAIN 512u
/* An idle task waits this long (ns) for a record. */
#define WAIT_NS 1000000ull

static void check(const mesh_ctx *ctx, const uint32_t w[12], uint64_t now) {
    uint64_t stamp = (uint64_t)w[MESH_PIPEDEMO_R_STAMP_LO] | (uint64_t)w[MESH_PIPEDEMO_R_STAMP_HI] << 32;
    uint32_t m[16];
    mesh_pipedemo_msg(w[MESH_PIPEDEMO_R_SEQ], stamp, w[MESH_PIPEDEMO_R_ORIGIN] & 0xffu, m);
    uint8_t bytes[64];
    for (int i = 0; i < 16; i++)
        for (int k = 0; k < 4; k++) bytes[4 * i + k] = (uint8_t)(m[i] >> (8 * k));
    uint8_t want[32], got[32];
    mesh_sha256d(bytes, sizeof bytes, want);
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 4; k++) got[4 * i + k] = (uint8_t)(w[MESH_PIPEDEMO_R_PAYLOAD + i] >> (24 - 8 * k));
    mesh_view v = mesh_ctx_view(ctx);
    mesh_view_add32(v, MESH_PIPEDEMO_K_RECORDS, 1);
    bool same = true;
    for (int i = 0; i < 32; i++) same = same && got[i] == want[i];
    if (!same) {
        mesh_view_add32(v, MESH_PIPEDEMO_K_BAD, 1);
        mesh_ctx_count(ctx, 1, 1);
        mesh_view_write_u32s(v, MESH_PIPEDEMO_K_LAST_BAD, w, 12);
    }
    if (w[MESH_PIPEDEMO_R_ORIGIN] & MESH_PIPEDEMO_PATH_GPU) {
        mesh_view_add32(v, MESH_PIPEDEMO_K_GPU, 1);
        mesh_ctx_count(ctx, 2, 1);
    } else {
        mesh_view_add32(v, MESH_PIPEDEMO_K_CPU, 1);
        mesh_ctx_count(ctx, 3, 1);
    }
    uint64_t d = now > stamp ? (now - stamp) / 1000 : 0;
    uint32_t us = d > UINT32_MAX ? UINT32_MAX : (uint32_t)d;
    mesh_view_add32(v, MESH_PIPEDEMO_K_HIST + 4 * (us < MESH_PIPEDEMO_K_BINS - 1 ? us : MESH_PIPEDEMO_K_BINS - 1), 1);
    if (us > mesh_view_u32(v, MESH_PIPEDEMO_K_MAX_US)) mesh_view_set_u32(v, MESH_PIPEDEMO_K_MAX_US, us);
}

static uint32_t hist[MESH_PIPEDEMO_K_BINS];

static void stats(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    mesh_view_read_u32s(v, MESH_PIPEDEMO_K_HIST, hist, MESH_PIPEDEMO_K_BINS);
    mesh_view_set_u32(v, MESH_PIPEDEMO_K_P50_US, hist_quantile(hist, MESH_PIPEDEMO_K_BINS, 500));
    mesh_view_set_u32(v, MESH_PIPEDEMO_K_P90_US, hist_quantile(hist, MESH_PIPEDEMO_K_BINS, 900));
    mesh_view_set_u32(v, MESH_PIPEDEMO_K_P99_US, hist_quantile(hist, MESH_PIPEDEMO_K_BINS, 990));
}

static uint32_t drain(const mesh_ctx *ctx) {
    uint32_t w[12];
    uint32_t n = 0, len = 0;
    /* An empty inbox: wait for a record (bounded, past the driver's tick) before the task ends. */
    uint64_t t0 = mesh_ctx_host_ns(ctx);
    while (!mesh_ctx_pipe_len(ctx, mesh_ctx_slot(ctx), &len) || len == 0) {
        if (mesh_ctx_host_ns(ctx) - t0 > WAIT_NS) {
            mesh_view_add32(mesh_ctx_view(ctx), MESH_PIPEDEMO_K_EMPTY, 1);
            return 0;
        }
    }
    while (n < DRAIN && mesh_ctx_pop_u32s(ctx, w, 12)) {
        check(ctx, w, mesh_ctx_host_ns(ctx));
        n++;
    }
    mesh_ctx_count(ctx, 0, n);
    if (mesh_ctx_ticket(ctx) % MESH_PIPEDEMO_K_STATS_EVERY == 0) stats(ctx);
    return n;
}

static void init(const mesh_ctx *ctx) { (void)ctx; }

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    (void)arg;
    switch (op) {
    case MESH_OP_TASK: return drain(ctx);
    case MESH_PIPEDEMO_OP_STATS: stats(ctx); return 0;
    case MESH_PIPEDEMO_OP_SINK_RESET: {
        mesh_view v = mesh_ctx_view(ctx);
        for (uint32_t o = 0; o < MESH_PIPEDEMO_K_HIST; o += 4) mesh_view_set_u32(v, o, 0);
        static const uint32_t zeros[MESH_PIPEDEMO_K_BINS];
        mesh_view_write_u32s(v, MESH_PIPEDEMO_K_HIST, zeros, MESH_PIPEDEMO_K_BINS);
        return 0;
    }
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
