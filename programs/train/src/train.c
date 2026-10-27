/* train: SGD of y = a + b x + c x^2 + d x^3, one sample a lane, gradients
 * butterfly-summed across the warp. In a lockstep group of N (manifest
 * `peer = "lockstep"`, H_GROUP_RANK k, H_GROUP_N N) each step's sums go
 * through the exchange page and every member applies slot0 + .. + slot(N-1)
 * in rank order: bitwise-identical params (data parallel, a batch of 32 N a
 * step). Layout: @ajax/mesh-abi-view-train. OP_INIT generates the samples on
 * the GPU (sin in f64 arithmetic only: the same bits on every IEEE machine).
 * The loops mirror mesh-kcheck's CPU reference index by index (bitwise checks).
 * Ported from alpha-rust-mesh programs/train/src/lib.rs. */
#include "mesh/rt.h"

static float train_cubic(const float p[4], float x) {
    float x2 = x * x;
    float x3 = x2 * x;
    return p[0] + p[1] * x + p[2] * x2 + p[3] * x3;
}

/* sin(pi x) for x in [-1, 1]: reflect into [-1/2, 1/2], then the Taylor
 * series to a^23 in Horner form (error < 1e-20). Only +, -, *, / in f64, so
 * the CPU copy gives the same bits. Not inlined (one shared copy). */
__attribute__((noinline)) static float train_sin_pi(float x) {
    double t = (double)x;
    if (t > 0.5) t = 1.0 - t;
    else if (t < -0.5) t = -1.0 - t;
    double a = 3.141592653589793 * t;
    double a2 = a * a;
    double s = 1.0;
    for (uint32_t k = 11; k >= 1; k--) s = 1.0 - a2 / (double)((2 * k) * (2 * k + 1)) * s;
    return (float)(a * s);
}

/* Sample i's x: -1 + 2 i / 255. */
static float train_sample_x(uint32_t i) { return -1.0f + 2.0f * (float)i / (float)(MESH_TRAIN_SAMPLES - 1); }

/* Lockstep in a group: this member's rank k, the group's size N, the exchange
 * page, when the manifest grants the page and meshd set H_GROUP_RANK and H_GROUP_N. */
static bool train_peer(const mesh_ctx *ctx, uint32_t *k, uint32_t *n, mesh_exchange *x) {
    return mesh_ctx_group(ctx, k, n) && mesh_ctx_exchange(ctx, x);
}

/* Fresh state: samples, hyperparameters, params and progress zero. The
 * exchange page is meshd's (zeroed while we are halted): clearing it here could
 * wipe a peer round mirrored in after the HELLO. */
static void train_init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    uint32_t l = mesh_ctx_lane(ctx);
    for (uint32_t i = l; i < MESH_TRAIN_SAMPLES; i += 32) {
        float x = train_sample_x(i);
        mesh_view_set_f32(v, MESH_TRAIN_T_X + 4 * i, x);
        mesh_view_set_f32(v, MESH_TRAIN_T_Y + 4 * i, train_sin_pi(x));
    }
    for (uint32_t j = l; j < 256; j += 32) mesh_view_set_f32(v, MESH_TRAIN_T_LOSS + 4 * j, 0.0f);
    if (mesh_ctx_leader(ctx)) {
        for (uint32_t k = 0; k < 4; k++) mesh_view_set_f32(v, MESH_TRAIN_T_PARAMS + 4 * k, 0.0f);
        mesh_view_set_f32(v, MESH_TRAIN_T_LR, MESH_TRAIN_LR);
        mesh_view_set_u32(v, MESH_TRAIN_T_STEPS_PER_TASK, MESH_TRAIN_STEPS_PER_TASK);
        /* A step's batch: 32 samples per system. */
        uint32_t k, n = 1;
        mesh_exchange x;
        if (!train_peer(ctx, &k, &n, &x)) n = 1;
        mesh_view_set_f32(v, MESH_TRAIN_T_INV_BATCH, 1.0f / (float)(32 * n));
        const uint32_t zero[5] = {MESH_TRAIN_T_NEXT_STEP, MESH_TRAIN_T_ERR, MESH_TRAIN_T_PENDING, MESH_TRAIN_T_ROUND, MESH_TRAIN_T_STEP_HI};
        for (int i = 0; i < 5; i++) mesh_view_set_u32(v, zero[i], 0);
        mesh_ctx_count(ctx, MESH_TRAIN_C_INITS, 1);
    }
}

/* Pick xs[j] for a warp-uniform j < 8 without a local-memory array. */
static inline __attribute__((always_inline)) float train_sel8(const float xs[8], uint32_t j) {
    float r = xs[0];
    for (uint32_t k = 1; k < 8; k++)
        if (j == k) r = xs[k];
    return r;
}

/* Round n with the cluster: publish own sums (unless resuming a round whose
 * slot and counter were already published), wait bounded for every other
 * system's, return slot0 + slot1 + ... + slot(N-1) folded in system order
 * (every machine then updates bitwise identically). False on timeout. */
static bool train_exchange(mesh_exchange ex, uint32_t l, uint32_t k, uint32_t nsys, uint64_t n, const float own[5], bool resuming,
                           float out[5]) {
    size_t par = MESH_EX_SLOT_BYTES * (size_t)(n % 2);
    uint8_t *mine = mesh_exchange_slots(ex, k) + par;
    uint32_t ok = 0;
    for (int j = 0; j < 5; j++) out[j] = 0.0f;
    if (l == 0) {
        if (!resuming) {
            for (int j = 0; j < 5; j++) mesh_rt_wrf(mine + 4 * j, own[j]);
            for (int j = 5; j < 8; j++) mesh_rt_wrf(mine + 4 * j, 0.0f);
            mesh_rt_fence_sc();
            mesh_rt_st_release64(mesh_exchange_counter(ex, k), n);
        }
        /* Bounded by PEER_POLLS polls and by their nominal duration (for all
         * members together): nanosleep may oversleep, the budget is wall time. */
        uint64_t deadline = mesh_rt_gtimer() + (uint64_t)MESH_PEER_POLLS * MESH_PEER_POLL_NS;
        uint32_t polls = 0;
        for (;;) {
            bool all = true;
            for (uint32_t j = 0; j < nsys; j++)
                if (j != k && mesh_rt_ld_acquire64(mesh_exchange_counter(ex, j)) < n) all = false;
            if (all) { ok = 1; break; }
            if (polls >= MESH_PEER_POLLS || mesh_rt_gtimer() >= deadline) break;
            mesh_rt_nanosleep(MESH_PEER_POLL_NS);
            polls++;
        }
        if (ok) {
            mesh_rt_fence_acq_rel();
            uint8_t *s0 = mesh_exchange_slots(ex, 0) + par;
            for (int j = 0; j < 5; j++) out[j] = mesh_rt_rdf(s0 + 4 * j);
            for (uint32_t m = 1; m < nsys; m++) {
                uint8_t *sm = mesh_exchange_slots(ex, m) + par;
                for (int j = 0; j < 5; j++) out[j] = out[j] + mesh_rt_rdf(sm + 4 * j);
            }
        }
    }
    if (mesh_rt_shfl_idx(ok, 0) == 0) return false;
    for (int j = 0; j < 5; j++) out[j] = mesh_rt_shfl_idx_f(out[j], 0);
    return true;
}

/* Arg s: the low half of the first step; the steps are base .. base +
 * T_STEPS_PER_TASK - 1 with base = T_STEP_HI:s (u64, so rounds never wrap).
 * Returns the steps completed. */
static uint32_t train_run(const mesh_ctx *ctx, uint32_t s) {
    mesh_view v = mesh_ctx_view(ctx);
    uint64_t base = (uint64_t)mesh_view_u32(v, MESH_TRAIN_T_STEP_HI) << 32 | s;
    uint32_t l = mesh_ctx_lane(ctx);
    float p[4] = {mesh_view_f32(v, MESH_TRAIN_T_PARAMS), mesh_view_f32(v, MESH_TRAIN_T_PARAMS + 4),
                  mesh_view_f32(v, MESH_TRAIN_T_PARAMS + 8), mesh_view_f32(v, MESH_TRAIN_T_PARAMS + 12)};
    float lr = mesh_view_f32(v, MESH_TRAIN_T_LR);
    float inv = mesh_view_f32(v, MESH_TRAIN_T_INV_BATCH);
    uint32_t steps = mesh_view_u32(v, MESH_TRAIN_T_STEPS_PER_TASK);
    uint32_t sys = 0, nsys = 1;
    mesh_exchange ex = {0};
    bool dual = train_peer(ctx, &sys, &nsys, &ex);
    if (!dual) { sys = 0; nsys = 1; }
    /* Lane l holds samples 8l..8l+7; a step's sample i = (8l + off) mod 256 is
     * then register (off & 7) of lane (l + off / 8) mod 32 -- a shuffle. One
     * bounds check (a short view faults), then unchecked loads. */
    mesh_view whole;
    if (!mesh_view_sub(v, 0, MESH_TRAIN_VIEW_BYTES, &whole)) return 0;
    uint8_t *vp = mesh_view_ptr(whole);
    float xs[8], ys[8];
    for (uint32_t j = 0; j < 8; j++) {
        xs[j] = mesh_rt_rdf(vp + MESH_TRAIN_T_X + 4 * (8 * l + j));
        ys[j] = mesh_rt_rdf(vp + MESH_TRAIN_T_Y + 4 * (8 * l + j));
    }
    /* A round left pending by a timed-out task is resumed, never recomputed. */
    bool resume = dual && mesh_view_u32(v, MESH_TRAIN_T_PENDING) != 0 && mesh_view_u32(v, MESH_TRAIN_T_ROUND) == s + 1;
    if (!resume && mesh_view_u32(v, MESH_TRAIN_T_PENDING) != 0 && l == 0) mesh_view_set_u32(v, MESH_TRAIN_T_PENDING, 0);

    uint32_t done = 0;
    bool timed_out = false;
    while (done < steps) {
        uint64_t t64 = base + done;
        uint32_t t = (uint32_t)t64; /* sample and loss indices only need t mod 256 */
        float sums[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        bool resuming = resume;
        resume = false;
        if (!resuming) {
            /* Step t's batch: offsets N t .. N t + N - 1, system k taking N t + k. */
            uint32_t off = t * nsys + sys;
            uint32_t src = (l + (off >> 3)) & 31u;
            float x = mesh_rt_shfl_idx_f(train_sel8(xs, off & 7u), src);
            float y = mesh_rt_shfl_idx_f(train_sel8(ys, off & 7u), src);
            float e = train_cubic(p, x) - y;
            float g = 2.0f * e;
            float x2 = x * x;
            sums[0] = mesh_rt_warp_sum(e * e);
            sums[1] = mesh_rt_warp_sum(g);
            sums[2] = mesh_rt_warp_sum(g * x);
            sums[3] = mesh_rt_warp_sum(g * x2);
            sums[4] = mesh_rt_warp_sum(g * (x2 * x));
        }
        if (dual) {
            float total[5];
            if (train_exchange(ex, l, sys, nsys, t64 + 1, sums, resuming, total)) {
                for (int j = 0; j < 5; j++) sums[j] = total[j];
            } else {
                timed_out = true;
                break;
            }
        }
        for (int k = 0; k < 4; k++) p[k] = p[k] - lr * sums[k + 1] * inv;
        if (l == 0) mesh_view_set_f32(v, MESH_TRAIN_T_LOSS + 4 * (t % 256), sums[0] * inv);
        done++;
    }
    if (l == 0) {
        for (uint32_t k = 0; k < 4; k++) mesh_view_set_f32(v, MESH_TRAIN_T_PARAMS + 4 * k, p[k]);
        if (timed_out) {
            mesh_view_set_u32(v, MESH_TRAIN_T_ROUND, s + done + 1);
            mesh_view_set_u32(v, MESH_TRAIN_T_PENDING, 1);
            mesh_ctx_count(ctx, MESH_TRAIN_C_TIMEOUTS, 1);
        } else if (dual) {
            mesh_view_set_u32(v, MESH_TRAIN_T_PENDING, 0);
        }
        mesh_view_set_u32(v, MESH_TRAIN_T_ERR, timed_out ? 1u : 0u);
        uint64_t next = base + done;
        mesh_view_set_u32(v, MESH_TRAIN_T_STEP_HI, (uint32_t)(next >> 32));
        mesh_view_set_u32(v, MESH_TRAIN_T_NEXT_STEP, (uint32_t)next);
        mesh_ctx_count(ctx, MESH_TRAIN_C_STEPS, done);
    }
    return done;
}

static uint32_t train_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    if (op == MESH_OP_TASK) return train_run(ctx, arg);
    if (op == MESH_TRAIN_OP_SET_LR) {
        if (mesh_ctx_leader(ctx)) mesh_ctx_set_f32(ctx, MESH_TRAIN_T_LR, mesh_f32_from_bits(arg));
        return MESH_R_NONE;
    }
    return MESH_R_BAD_OP;
}

MESH_PROGRAM(train_init, train_task);
