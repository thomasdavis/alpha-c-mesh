/* pipehash: the pipe demo's GPU stage (@ajax/mesh-abi-view-pipedemo, H_*). A
 * task (one warp) finds the sink (pipesink, else pipesink-w), pushes the
 * digests its node carried from an earlier task, then -- if the sink has room
 * for 32 more -- pops up to 32 `msg`s from its own inbox (lane i keeps the
 * i-th), hashes them in parallel (sha256d of the 64 bytes, pipedemo digest)
 * and pushes the digests on, one collective push each. A digest the sink does
 * not take within PUSH_WAIT_NS goes into the node's carry in the view, so
 * nothing is dropped and the next task retries it first. Without room the
 * warp pops nothing: backpressure up the pipe.
 * Counters: 0 hashed (pushed on), 1 stalls, 2 carried.
 * Ported from alpha-rust-mesh programs/pipehash/src/lib.rs. */
#include "mesh/rt.h"

/* An idle task waits this long for work (ns) before it ends: past the
 * driver's 500 us tick, so a warp is always waiting on the inbox. */
#define PIPEHASH_WAIT_NS UINT64_C(1000000)
/* A push into a full sink retries this long (ns) before the digest is carried. */
#define PIPEHASH_PUSH_WAIT_NS UINT64_C(200000)
#define PIPEHASH_DIGEST_WORDS (MESH_PIPEDEMO_DIGEST_BYTES / 4)

static void pipehash_init(const mesh_ctx *ctx) { (void)ctx; }

typedef struct {
    uint32_t lane, owner;
    const uint32_t *dg;
} pipehash_fill;
static void pipehash_fill_rec(mesh_view v, void *user) {
    const pipehash_fill *f = user;
    if (f->lane == f->owner)
        for (uint32_t k = 0; k < 12; k++) mesh_view_set_u32(v, 4 * k, f->dg[k]);
}

/* Pushes the digest that lane owner holds (dg) into slot d, retrying a full
 * sink for PUSH_WAIT_NS: true pushed. */
static bool pipehash_push(const mesh_ctx *ctx, uint32_t d, uint32_t owner, const uint32_t dg[12]) {
    pipehash_fill f = {mesh_ctx_lane(ctx), owner, dg};
    uint64_t t0 = mesh_rt_gtimer();
    for (;;) {
        int32_t code = mesh_ctx_push_with(ctx, d, pipehash_fill_rec, &f);
        if (code == MESH_PIPE_OK) return true;
        if (code != MESH_PIPE_NONE || mesh_rt_shfl_idx(mesh_rt_gtimer() - t0 > PIPEHASH_PUSH_WAIT_NS ? 1u : 0u, 0) != 0) return false;
        mesh_rt_nanosleep(2000);
    }
}

static uint32_t pipehash_rec(uint32_t j) { return MESH_PIPEDEMO_H_CARRY_REC + j * MESH_PIPEDEMO_DIGEST_BYTES; }

/* The node's carried digests go first; false: some are still carried. */
static bool pipehash_push_carry(const mesh_ctx *ctx, bool have_d, uint32_t d, mesh_view carry) {
    uint32_t held = mesh_rt_shfl_idx(mesh_view_u32(carry, 0), 0);
    if (held == 0) return true;
    uint32_t lane = mesh_ctx_lane(ctx), i = 0;
    if (have_d) {
        while (i < held) {
            uint32_t dg[12] = {0};
            if (lane == 0)
                for (uint32_t k = 0; k < 12; k++) dg[k] = mesh_view_u32(carry, pipehash_rec(i) + 4 * k);
            if (!pipehash_push(ctx, d, 0, dg)) break;
            i++;
        }
    }
    if (i == 0) return false;
    /* Keep the rest at the front (lane k copies word k of each record, front to back). */
    for (uint32_t j = i; j < held; j++)
        if (lane < PIPEHASH_DIGEST_WORDS) mesh_view_set_u32(carry, pipehash_rec(j - i) + 4 * lane, mesh_view_u32(carry, pipehash_rec(j) + 4 * lane));
    if (mesh_ctx_leader(ctx)) {
        mesh_view_set_u32(carry, 0, held - i);
        mesh_ctx_count(ctx, 0, i);
    }
    return i == held;
}

typedef struct {
    uint32_t lane, i;
    uint32_t *m;
} pipehash_take;
static void pipehash_take_rec(mesh_view v, void *user) {
    pipehash_take *t = user;
    if (t->lane == t->i)
        for (uint32_t k = 0; k < 16; k++) t->m[k] = mesh_view_u32(v, 4 * k);
}

static uint32_t pipehash_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    (void)arg;
    if (op != MESH_OP_TASK) return MESH_R_BAD_OP;
    uint32_t lane = mesh_ctx_lane(ctx), node = mesh_ctx_node(ctx);
    if (node >= MESH_PIPEDEMO_H_NODES_MAX) return MESH_R_BAD_OP;
    mesh_view carry;
    if (!mesh_view_sub(mesh_ctx_view(ctx), MESH_PIPEDEMO_H_CARRY + node * MESH_PIPEDEMO_H_CARRY_BYTES, MESH_PIPEDEMO_H_CARRY_BYTES, &carry))
        return MESH_R_BAD_OP;
    /* Every decision the warp takes together is lane 0's, broadcast: lanes
     * reading the table or a ring's words at different moments could disagree,
     * and a collective (push_with, pop_with) needs all 32. */
    uint32_t d = UINT32_MAX, s;
    if (mesh_ctx_find(ctx, "pipesink", 8, &s) || mesh_ctx_find(ctx, "pipesink-w", 10, &s)) d = s;
    d = mesh_rt_shfl_idx(d, 0);
    bool have_d = d != UINT32_MAX;
    if (mesh_ctx_leader(ctx)) mesh_ctx_set_u32(ctx, MESH_PIPEDEMO_H_DST, have_d ? d + 1 : 0);
    if (!pipehash_push_carry(ctx, have_d, d, carry)) {
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 1, 1);
        mesh_rt_nanosleep(20000);
        return 0;
    }
    if (!have_d) {
        mesh_rt_nanosleep(20000);
        return 0;
    }
    /* Room for a warp's 32 digests in the sink, and something to hash: else wait (bounded). */
    uint64_t t0 = mesh_rt_gtimer();
    for (;;) {
        uint32_t free_cells = 0, waiting = 0;
        bool room = mesh_ctx_pipe_free(ctx, d, &free_cells) && free_cells >= 32;
        bool work = mesh_ctx_pipe_len(ctx, mesh_ctx_slot(ctx), &waiting) && waiting > 0;
        if (room && work) break;
        if (mesh_rt_shfl_idx(mesh_rt_gtimer() - t0 > PIPEHASH_WAIT_NS ? 1u : 0u, 0) != 0) {
            if (!room && mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 1, 1);
            return 0;
        }
        mesh_rt_nanosleep(1000);
    }
    uint32_t m[16] = {0}, n = 0;
    while (n < 32) {
        pipehash_take t = {lane, n, m};
        if (mesh_ctx_pop_with(ctx, pipehash_take_rec, &t) != MESH_PIPE_OK) break;
        n++;
    }
    if (n == 0) return 0;
    uint32_t dg[12] = {0};
    if (lane < n) mesh_pipedemo_digest(m, MESH_PIPEDEMO_PATH_GPU, dg);
    uint32_t i = 0;
    while (i < n && pipehash_push(ctx, d, i, dg)) i++;
    /* The sink did not take the rest in time: lane j's digest into carry slot j - i. */
    if (lane >= i && lane < n)
        for (uint32_t k = 0; k < 12; k++)
            mesh_view_set_u32(carry, MESH_PIPEDEMO_H_CARRY_REC + (lane - i) * MESH_PIPEDEMO_DIGEST_BYTES + 4 * k, dg[k]);
    if (mesh_ctx_leader(ctx)) {
        mesh_view_set_u32(carry, 0, n - i);
        mesh_ctx_count(ctx, 0, i);
        if (i < n) mesh_ctx_count(ctx, 2, n - i);
    }
    return n;
}

MESH_PROGRAM(pipehash_init, pipehash_task);
