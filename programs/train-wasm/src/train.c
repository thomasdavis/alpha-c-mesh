/* train for the wasm executor (alpha-rust-mesh programs/train/wasm): SGD of
 * y = a + b x + c x^2 + d x^3 on 256 samples of sin(pi x), lockstep with the cluster through the
 * exchange page, in the cluster float mode. The warp of the GPU image -- 32 lanes, one sample each,
 * butterfly-summed -- is mesh-ref's step_sums, written operation for operation as the GPUs compute
 * (Z: every f32 op flushed), so the params, losses and exchanged sums are the GPUs' bits (kcheck X:
 * the cross-arch digests). Layout: mesh-abi views::train. */
#include "mesh/abi/view_train.h"
#include "mesh/ref/cubic.h"
#include "mesh/rt_wasm.h"

/* Lockstep in a cluster: (this system k, the size N, the exchange page). */
static bool peer_system(const mesh_ctx *ctx, uint32_t *k, uint32_t *n, mesh_exchange *x) {
    return mesh_ctx_group(ctx, k, n) && mesh_ctx_exchange(ctx, x);
}

static float xs[MESH_TRAIN_SAMPLES], ys[MESH_TRAIN_SAMPLES];

/* Fresh state: samples (sin_pi in f64, the f32 grid in Z: the GPUs' bits), hyperparameters,
 * params and progress zero. The exchange page is meshd's. */
static void init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    mesh_cubic_samples(xs, ys);
    mesh_view_write_f32s(v, MESH_TRAIN_T_X, xs, MESH_TRAIN_SAMPLES);
    mesh_view_write_f32s(v, MESH_TRAIN_T_Y, ys, MESH_TRAIN_SAMPLES);
    static const float zeros[256];
    mesh_view_write_f32s(v, MESH_TRAIN_T_LOSS, zeros, 256);
    mesh_view_write_f32s(v, MESH_TRAIN_T_PARAMS, zeros, 4);
    mesh_view_set_f32(v, MESH_TRAIN_T_LR, MESH_TRAIN_LR);
    mesh_view_set_u32(v, MESH_TRAIN_T_STEPS_PER_TASK, MESH_TRAIN_STEPS_PER_TASK);
    /* A step's batch: 32 samples per system. */
    uint32_t k, n = 1;
    mesh_exchange x;
    if (!peer_system(ctx, &k, &n, &x)) n = 1;
    mesh_view_set_f32(v, MESH_TRAIN_T_INV_BATCH, mesh_ftz_div(1.0f, mesh_ftz_from_u32(32 * n)));
    const uint32_t zero[5] = {MESH_TRAIN_T_NEXT_STEP, MESH_TRAIN_T_ERR, MESH_TRAIN_T_PENDING, MESH_TRAIN_T_ROUND,
                              MESH_TRAIN_T_STEP_HI};
    for (int i = 0; i < 5; i++) mesh_view_set_u32(v, zero[i], 0);
    mesh_ctx_count(ctx, MESH_TRAIN_C_INITS, 1);
}

/* Round n with the cluster: publish own sums (unless resuming a round whose slot and counter were
 * already published), wait bounded for every other system's, return slot0 + slot1 + ... + slot(N-1)
 * folded in system order (in Z: every member updates bitwise identically). false on timeout. */
static bool exchange(mesh_exchange ex, uint32_t k, uint32_t nsys, uint64_t n, const float own[5], bool resuming,
                     float out[5]) {
    uint32_t par = (uint32_t)(n % 2);
    if (!resuming) {
        uint32_t w[MESH_EX_SLOT_WORDS] = {0};
        for (int j = 0; j < 5; j++) w[j] = mesh_ftz_bits(own[j]);
        mesh_exchange_set_slot(ex, k, par, w);
    }
    if (!mesh_exchange_round(ex, n)) return false;
    uint32_t s[MESH_EX_SLOT_WORDS];
    mesh_exchange_slot(ex, 0, par, s);
    for (int j = 0; j < 5; j++) out[j] = mesh_ftz_from_bits(s[j]);
    for (uint32_t m = 1; m < nsys; m++) {
        mesh_exchange_slot(ex, m, par, s);
        for (int j = 0; j < 5; j++) out[j] = mesh_ftz_add(out[j], mesh_ftz_from_bits(s[j]));
    }
    return true;
}

/* Arg s: the low half of the first step; the steps are base .. base + T_STEPS_PER_TASK - 1 with
 * base = T_STEP_HI:s. Returns the steps completed. */
static uint32_t run(const mesh_ctx *ctx, uint32_t s) {
    mesh_view v = mesh_ctx_view(ctx);
    uint64_t base = (uint64_t)mesh_view_u32(v, MESH_TRAIN_T_STEP_HI) << 32 | s;
    uint32_t hdr[12];
    mesh_view_read_u32s(v, 0, hdr, 12);
    float p[4] = {mesh_ftz_from_bits(hdr[0]), mesh_ftz_from_bits(hdr[1]), mesh_ftz_from_bits(hdr[2]), mesh_ftz_from_bits(hdr[3])};
    float lr = mesh_ftz_from_bits(hdr[4]), inv = mesh_ftz_from_bits(hdr[6]);
    uint32_t steps = hdr[5];
    uint32_t pending = hdr[9], round = hdr[10];
    uint32_t sys = 0, nsys = 1;
    mesh_exchange ex;
    bool dual = peer_system(ctx, &sys, &nsys, &ex);
    if (!dual) sys = 0, nsys = 1;
    mesh_view_read_f32s(v, MESH_TRAIN_T_X, xs, MESH_TRAIN_SAMPLES);
    mesh_view_read_f32s(v, MESH_TRAIN_T_Y, ys, MESH_TRAIN_SAMPLES);
    /* A round left pending by a timed-out task is resumed, never recomputed. */
    bool resume = dual && pending != 0 && round == s + 1u;
    if (!resume && pending != 0) mesh_view_set_u32(v, MESH_TRAIN_T_PENDING, 0);
    uint32_t done = 0;
    bool timed_out = false;
    while (done < steps) {
        uint64_t t64 = base + done;
        uint32_t t = (uint32_t)t64;
        float sums[5] = {0, 0, 0, 0, 0};
        bool resuming = resume;
        resume = false;
        /* Step t's batch: system k takes offset N t + k. */
        if (!resuming) mesh_cubic_step_sums(p, xs, ys, t * nsys + sys, sums);
        if (dual) {
            float total[5];
            if (!exchange(ex, sys, nsys, t64 + 1, sums, resuming, total)) {
                timed_out = true;
                break;
            }
            for (int j = 0; j < 5; j++) sums[j] = total[j];
        }
        mesh_cubic_apply(p, sums, lr, inv);
        mesh_view_set_f32(v, MESH_TRAIN_T_LOSS + 4 * (t % 256), mesh_cubic_loss(sums, inv));
        done++;
    }
    mesh_view_write_f32s(v, MESH_TRAIN_T_PARAMS, p, 4);
    if (timed_out) {
        mesh_view_set_u32(v, MESH_TRAIN_T_ROUND, s + done + 1u);
        mesh_view_set_u32(v, MESH_TRAIN_T_PENDING, 1);
        mesh_ctx_count(ctx, MESH_TRAIN_C_TIMEOUTS, 1);
    } else if (dual) {
        mesh_view_set_u32(v, MESH_TRAIN_T_PENDING, 0);
    }
    mesh_view_set_u32(v, MESH_TRAIN_T_ERR, timed_out);
    uint64_t next = base + done;
    mesh_view_set_u32(v, MESH_TRAIN_T_STEP_HI, (uint32_t)(next >> 32));
    mesh_view_set_u32(v, MESH_TRAIN_T_NEXT_STEP, (uint32_t)next);
    mesh_ctx_count(ctx, MESH_TRAIN_C_STEPS, done);
    return done;
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    switch (op) {
    case MESH_OP_TASK: return run(ctx, arg);
    case MESH_TRAIN_OP_SET_LR: mesh_ctx_set_f32(ctx, MESH_TRAIN_T_LR, mesh_ftz_from_bits(arg)); return MESH_R_NONE;
    default: return MESH_R_BAD_OP;
    }
}

MESH_PROGRAM(init, task)
