/* pathtrace: a deterministic path tracer (the pathtrace-* packages), one work
 * item a task, one tile column a lane. The final image is bit for bit the same
 * whatever renders each part -- a GB10, an RTX 3060, an Apple GPU, a CPU lane
 * (wasm) or the CPU reference (mesh-ref) -- because every float op is
 * correctly rounded, unfused and FTZ (the cluster float mode), the random
 * numbers are a hash of (seed, pixel, sample, dimension), and a pixel's samples
 * are summed in a fixed order.
 *
 * A node is `warps` warps: the scene goes into the node's shared memory, every
 * thread renders pixels thread, thread + 32 warps, .. of the tile's pass (each
 * pixel's samples in order from +0, as pixel_pass: the bits do not depend on
 * who renders it) into shared memory, and after a block barrier warp 0 -- one
 * column a lane -- does the rest: digests, payload, fold, tone map. Ops that
 * render nothing run on warp 0 alone.
 *
 * A work item is (tile, pass), w = pass x tiles + tile: it computes the pass
 * sums P_p of its tile from +0, then folds them into the tile's accumulators in
 * pass order (claim: M_PASSES p -> p | FOLDING by CAS, waiting bounded for pass
 * p - 1), so a tile's accumulator is always ((0 + P0) + P1) + .., however its
 * items were scheduled. pathtrace (policy job): OP_JOB item job_base + arg; a
 * fleet job: OP_PASS, the pass sums only into the payload slot (no fold, no
 * wait); pathtrace-live (policy periodic, arg = view_u32:V_NEXT): OP_TASK item
 * V_NEXT + index, the whole image refining pass by pass.
 *
 * Ops: OP_INIT, OP_TASK, OP_JOB, OP_REQ (a scene's raw words: validated,
 * loaded, restart), OP_RESTART, OP_TONEMAP arg (re-tone every tile from its
 * accumulators), OP_DEFAULT, OP_PASS, OP_SET.
 * Ported from alpha-rust-mesh programs/pathtrace/src/lib.rs. */
#include "mesh/rt.h"
#include "pathtrace/default_scene.h"
#include "pathtrace/digest.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"

/* The renderer with the device's own f32 operations (pathtrace-core's
 * `impl Real for f32`): mesh-ptx-link makes every one .ftz on the GPU, and the
 * simulator runs them in flush-to-zero mode on the CPU. */
#define PT_SUFFIX _dev
#define PT_API static inline __attribute__((always_inline))
#define PT_ADD(a, b) ((a) + (b))
#define PT_SUB(a, b) ((a) - (b))
#define PT_MUL(a, b) ((a) * (b))
#define PT_DIV(a, b) ((a) / (b))
#define PT_NEG(a) (-(a))
#define PT_LT(a, b) ((a) < (b))
#define PT_GT(a, b) ((a) > (b))
#define PT_LE(a, b) ((a) <= (b))
#define PT_GE(a, b) ((a) >= (b))
#include "pathtrace/instantiate.h"

#define PTP_EXEC PT_EXEC_CUDA

/* Each node's copy of the scene, in device memory: the renderer reads it
 * thousands of times a sample, every lane the same word (one cached broadcast
 * load), where a copy per lane (local memory) or the view (host memory) is
 * many times slower. Scratch, rewritten by every task. */
static uint32_t ptp_scenes[MESH_PROG_MAX_NODES][PT_SCENE_MAX_WORDS];

/* The node's shared memory (manifest shared_bytes): the scene, then the tile's pass sums (r g b a pixel, row-major). */
#define PTP_SH_SCENE 0u
#define PTP_SH_PART (4u * PT_SCENE_MAX_WORDS)
#define PTP_SH_BYTES (PTP_SH_PART + 12u * PT_TILE_PIX)

static uint32_t ptp_clamp_words(uint32_t w) { return w < PT_HEADER_WORDS ? PT_HEADER_WORDS : w > PT_SCENE_MAX_WORDS ? PT_SCENE_MAX_WORDS : w; }

/* Block-collective: the view's scene into the node's shared memory
 * (block-strided), validated (at most max_tiles; every thread the same answer):
 * its words, or NULL when the view holds no valid scene. */
static const uint32_t *ptp_scene_shared(const mesh_ctx *ctx, mesh_view v, uint32_t max_tiles, uint32_t *words_out) {
    uint32_t words = ptp_clamp_words(mesh_view_u32(v, PT_V_SCENE + 4 * PT_H_WORDS));
    mesh_shared sh;
    if (!mesh_ctx_shared(ctx, PTP_SH_SCENE, PT_SCENE_MAX_WORDS, 4, &sh)) return NULL;
    MESH_CTX_FOR_STRIDED(ctx, words, i) mesh_shared_set_u32(sh, i, mesh_view_u32(v, PT_V_SCENE + 4 * i));
    mesh_ctx_block_barrier(ctx);
    const uint32_t *sc = mesh_shared_ptr_u32(sh);
    *words_out = words;
    return pt_validate_in(sc, words, max_tiles) == 0 ? sc : NULL;
}

/* Block-collective: pass p of tile t, every pixel's samples in order from +0
 * (pixel_pass), pixel i = row x 32 + column by thread i mod threads, into the
 * node's shared pass sums; complete once it returns. */
static bool ptp_render_pass(const mesh_ctx *ctx, const uint32_t *sc, const pt_layout *l, uint32_t t, uint32_t p, mesh_shared *part) {
    if (!mesh_ctx_shared(ctx, PTP_SH_PART, 3 * PT_TILE_PIX, 4, part)) return false;
    uint32_t x0, y0;
    pt_tile_origin(sc, t, &x0, &y0);
    MESH_CTX_FOR_STRIDED(ctx, PT_TILE_PIX, i) {
        pt_v s = pt_pixel_pass_dev(sc, l, x0 + i % PT_TILE_W, y0 + i / PT_TILE_W, p);
        mesh_shared_set_unchecked_f32(*part, 3 * i, s.x);
        mesh_shared_set_unchecked_f32(*part, 3 * i + 1, s.y);
        mesh_shared_set_unchecked_f32(*part, 3 * i + 2, s.z);
    }
    mesh_ctx_block_barrier(ctx);
    return true;
}

/* Column cx of the shared pass sums (warp 0: lane = column) and its digest. */
static uint64_t ptp_shared_col(mesh_shared part, uint32_t cx, pt_v out[PT_TILE_H]) {
    for (uint32_t row = 0; row < PT_TILE_H; row++) {
        uint32_t i = 3 * (row * PT_TILE_W + cx);
        out[row] = (pt_v){mesh_shared_get_f32(part, i), mesh_shared_get_f32(part, i + 1), mesh_shared_get_f32(part, i + 2)};
    }
    return pt_col_digest_dev(out);
}

/* Words 0..n of src (the view at off, or the request window) into this node's
 * scene copy; the copy, once every lane's words are in. */
static const uint32_t *ptp_copy_words(const mesh_ctx *ctx, mesh_view src, uint32_t off, uint32_t n) {
    uint32_t node = mesh_ctx_node(ctx) < MESH_PROG_MAX_NODES - 1 ? mesh_ctx_node(ctx) : MESH_PROG_MAX_NODES - 1;
    uint32_t *row = ptp_scenes[node];
    for (uint32_t i = mesh_ctx_lane(ctx); i < n; i += 32) row[i] = mesh_view_u32(src, off + 4 * i);
    mesh_rt_warp_sync();
    return row;
}

/* The view's scene into this node's copy (lane-strided loads), validated (at
 * most max_tiles): the copy's words, or NULL (nothing rendered). */
static const uint32_t *ptp_scene(const mesh_ctx *ctx, mesh_view v, uint32_t max_tiles) {
    uint32_t words = ptp_clamp_words(mesh_view_u32(v, PT_V_SCENE + 4 * PT_H_WORDS));
    const uint32_t *sc = ptp_copy_words(ctx, v, PT_V_SCENE, words);
    return pt_validate_in(sc, words, max_tiles) == 0 ? sc : NULL;
}

/* This lane's column of the pass sums into the item's payload slot (ticket %
 * 64), row-major r g b, before the task returns. */
static void ptp_put_payload(const mesh_ctx *ctx, mesh_view v, const pt_v part[PT_TILE_H]) {
    uint32_t base = pt_view_payload(mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS) + 12 * mesh_ctx_lane(ctx);
    for (uint32_t row = 0; row < PT_TILE_H; row++) {
        uint32_t o = base + 12 * PT_TILE_W * row;
        mesh_view_set_f32(v, o, part[row].x);
        mesh_view_set_f32(v, o + 4, part[row].y);
        mesh_view_set_f32(v, o + 8, part[row].z);
    }
}

/* Writes the scene's header copies and zeroes the progress (a new image). */
static void ptp_restart(const mesh_ctx *ctx, mesh_view v, const uint32_t *sc) {
    uint32_t l = mesh_ctx_lane(ctx), tiles = pt_tiles(sc);
    for (uint32_t i = l; i < PT_MAX_TILES * PT_META / 4; i += 32) mesh_view_set_u32(v, PT_V_META + 4 * i, 0);
    /* A view without tiles (pathtrace-cpu's) has no pixels to clear. */
    for (uint32_t i = l; mesh_view_len(v) >= PT_VIEW_BYTES && i < tiles * PT_TILE_PIX; i += 32) mesh_view_set_u32(v, PT_V_RGB + 4 * i, 0);
    if (mesh_ctx_leader(ctx)) {
        const uint32_t kv[10][2] = {{PT_V_NEXT, 0}, {PT_V_DONE, 0}, {PT_V_TILES, tiles}, {PT_V_WIDTH, sc[PT_H_WIDTH]}, {PT_V_HEIGHT, sc[PT_H_HEIGHT]},
                                    {PT_V_SPP, sc[PT_H_SPP]}, {PT_V_PASSES, sc[PT_H_PASSES]}, {PT_V_LAST_TILE, 0}, {PT_V_LAST_SYSTEM, 0}, {PT_V_LAST_EXEC, 0}};
        for (int i = 0; i < 10; i++) mesh_view_set_u32(v, kv[i][0], kv[i][1]);
        mesh_view_set_u32(v, PT_V_GEN, mesh_view_u32(v, PT_V_GEN) + 1);
        mesh_view_set_u32(v, PT_V_MAGIC, PT_VIEW_MAGIC);
    }
}

static void ptp_load_default(const mesh_ctx *ctx, mesh_view v) {
    for (uint32_t i = mesh_ctx_lane(ctx); i < PT_DEFAULT_SCENE_WORDS; i += 32) mesh_view_set_u32(v, PT_V_SCENE + 4 * i, pt_default_scene[i]);
    ptp_restart(ctx, v, pt_default_scene);
}

static void ptp_init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    if (mesh_ctx_leader(ctx)) {
        mesh_view_set_u32(v, PT_V_TONEMAP, PT_TONEMAP_ACES);
        mesh_view_set_u32(v, PT_V_SCENE_ERR, 0);
    }
    ptp_load_default(ctx, v);
}

static uint32_t ptp_acc_at(uint32_t t, uint32_t cx) { return pt_view_acc(t) + 12 * cx; }

static void ptp_load_col(mesh_view v, uint32_t t, uint32_t cx, pt_v acc[PT_TILE_H]) {
    uint32_t base = ptp_acc_at(t, cx);
    for (uint32_t row = 0; row < PT_TILE_H; row++) {
        uint32_t o = base + 12 * PT_TILE_W * row;
        acc[row] = (pt_v){mesh_view_f32(v, o), mesh_view_f32(v, o + 4), mesh_view_f32(v, o + 8)};
    }
}

static void ptp_store_rgb(mesh_view v, uint32_t t, uint32_t cx, const uint32_t rgb[PT_TILE_H]) {
    for (uint32_t row = 0; row < PT_TILE_H; row++) mesh_view_set_u32(v, pt_view_rgb(t) + 4 * (PT_TILE_W * row + cx), rgb[row]);
}

/* The 32 lanes' column digests folded in column order (pathtrace-core fold), on every lane. */
static uint64_t ptp_fold(uint64_t h) {
    uint32_t lo = (uint32_t)h, hi = (uint32_t)(h >> 32);
    uint64_t cols[32];
    for (uint32_t j = 0; j < 32; j++) cols[j] = (uint64_t)mesh_rt_shfl_idx(lo, j) | (uint64_t)mesh_rt_shfl_idx(hi, j) << 32;
    return pt_fold(cols);
}

/* How long an item waits for its tile's previous pass to be folded (a lease
 * runs items in order, so a wait is short: a node still on pass p - 1). */
#define PTP_WAIT_NS UINT64_C(2000000000)
#define PTP_CLAIMED 0u
#define PTP_ALREADY 1u
#define PTP_TIMEOUT 2u

/* Lane 0 claims tile t's pass p for folding (M_PASSES: p -> p | FOLDING),
 * waiting bounded for passes 0..p; the outcome on every lane. */
static uint32_t ptp_claim(const mesh_ctx *ctx, mesh_view v, uint32_t t, uint32_t p) {
    uint32_t out = PTP_TIMEOUT;
    mesh_view w;
    if (mesh_ctx_leader(ctx) && mesh_view_sub(v, pt_view_meta(t) + PT_M_PASSES, 4, &w)) {
        uint64_t t0 = mesh_rt_gtimer();
        for (;;) {
            uint32_t cur = mesh_rt_ld_acquire(mesh_view_ptr(w));
            if (cur == p && mesh_rt_atom_cas(mesh_view_ptr(w), p, p | PT_FOLDING) == p) { out = PTP_CLAIMED; break; }
            if ((cur & ~PT_FOLDING) > p || ((cur & PT_FOLDING) == 0 && cur > p)) { out = PTP_ALREADY; break; }
            if (mesh_rt_gtimer() - t0 > PTP_WAIT_NS) break;
            mesh_rt_nanosleep(2000);
        }
    }
    out = mesh_rt_shfl_idx(out, 0);
    /* Lane 0's acquire, then the warp barrier: every lane reads the accumulators after it. */
    mesh_rt_warp_sync();
    return out;
}

static void ptp_bump(mesh_view v, uint32_t off) {
    mesh_view w;
    if (mesh_view_sub(v, off, 4, &w)) mesh_rt_atom_add(mesh_view_ptr(w), 1);
}

/* Work item (tile t, pass p): the pass sums from +0 (lane = column), then, in
 * pass order, folded into the tile's accumulators and tone mapped. False on a
 * timeout; else *dp_out the pass sums' digest and *da_out the accumulator
 * digest after the fold (0 when the pass was folded already -- a re-run). */
static bool ptp_item(const mesh_ctx *ctx, mesh_view v, const uint32_t *sc, mesh_shared part_sh, uint32_t t, uint32_t p, uint32_t tm,
                     uint64_t t0, bool pay, uint64_t *dp_out, uint64_t *da_out) {
    uint32_t cx = mesh_ctx_lane(ctx);
    pt_v part[PT_TILE_H];
    uint64_t dp = ptp_fold(ptp_shared_col(part_sh, cx, part));
    if (pay) ptp_put_payload(ctx, v, part);
    uint32_t c = ptp_claim(ctx, v, t, p);
    if (c == PTP_ALREADY) { *dp_out = dp; *da_out = 0; return true; }
    if (c != PTP_CLAIMED) return false;
    pt_v acc[PT_TILE_H];
    if (p > 0) ptp_load_col(v, t, cx, acc);
    else
        for (uint32_t r = 0; r < PT_TILE_H; r++) acc[r] = (pt_v){0.0f, 0.0f, 0.0f};
    uint32_t rgb[PT_TILE_H];
    uint64_t ha, hr;
    pt_fold_column_dev(sc, p + 1, tm, acc, part, rgb, &ha, &hr);
    uint32_t base = ptp_acc_at(t, cx);
    for (uint32_t row = 0; row < PT_TILE_H; row++) {
        uint32_t o = base + 12 * PT_TILE_W * row;
        mesh_view_set_f32(v, o, acc[row].x);
        mesh_view_set_f32(v, o + 4, acc[row].y);
        mesh_view_set_f32(v, o + 8, acc[row].z);
    }
    ptp_store_rgb(v, t, cx, rgb);
    uint64_t da = ptp_fold(ha), dr = ptp_fold(hr);
    /* Every lane's words before lane 0 releases the pass. */
    mesh_rt_fence_acq_rel();
    mesh_rt_warp_sync();
    if (mesh_ctx_leader(ctx)) {
        uint32_t m = pt_view_meta(t);
        uint32_t us = (uint32_t)((mesh_rt_gtimer() - t0) / 1000);
        uint32_t exec = PTP_EXEC | mesh_ctx_node(ctx) << 8;
        const uint32_t ws[7] = {mesh_ctx_system(ctx), exec, us, (uint32_t)da, (uint32_t)(da >> 32), (uint32_t)dr, (uint32_t)(dr >> 32)};
        for (uint32_t i = 0; i < 7; i++) mesh_view_set_u32(v, m + 4 + 4 * i, ws[i]);
        mesh_view_set_u32(v, PT_V_LAST_TILE, t);
        mesh_view_set_u32(v, PT_V_LAST_SYSTEM, mesh_ctx_system(ctx));
        mesh_view_set_u32(v, PT_V_LAST_EXEC, exec);
        mesh_rt_fence_acq_rel();
        mesh_view w;
        if (mesh_view_sub(v, m + PT_M_PASSES, 4, &w)) mesh_rt_st_release(mesh_view_ptr(w), p + 1);
        ptp_bump(v, PT_V_ITEMS);
        mesh_ctx_count(ctx, 0, 1);
        mesh_ctx_count(ctx, 2, 1);
        if (p + 1 == sc[PT_H_PASSES]) {
            mesh_ctx_count(ctx, 1, 1);
            ptp_bump(v, PT_V_DONE);
        }
    }
    *dp_out = dp;
    *da_out = da;
    return true;
}

static void ptp_job_record(const mesh_ctx *ctx, mesh_view v, uint32_t t, uint32_t p, uint64_t dp, uint64_t da, uint64_t t0) {
    uint32_t rec = PT_V_JOB_OUT + PT_JOB_REC * (mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS);
    uint32_t us = (uint32_t)((mesh_rt_gtimer() - t0) / 1000);
    const uint32_t x[8] = {t, p, (uint32_t)dp, (uint32_t)(dp >> 32), (uint32_t)da, (uint32_t)(da >> 32), us, PTP_EXEC | mesh_ctx_node(ctx) << 8};
    for (uint32_t i = 0; i < 8; i++) mesh_view_set_u32(v, rec + 4 * i, x[i]);
}

/* OP_TASK, OP_JOB, OP_PASS on the whole node: the scene and the pass sums in
 * shared memory (every thread), then warp 0's column work. */
static uint32_t ptp_render(const mesh_ctx *ctx, mesh_view v, uint32_t op, uint32_t arg) {
    bool job = op == MESH_OP_JOB || op == PT_OP_PASS;
    uint32_t fail = job ? MESH_R_JOB_FAIL : MESH_R_BAD_OP;
    if (PTP_SH_BYTES > mesh_ctx_shared_len(ctx)) return fail;
    uint32_t words;
    const uint32_t *sc = ptp_scene_shared(ctx, v, op == PT_OP_PASS ? PT_FLEET_MAX_TILES : PT_MAX_TILES, &words);
    if (!sc) return fail;
    pt_layout l = pt_layout_of(sc);
    uint32_t tiles = pt_tiles(sc), passes = sc[PT_H_PASSES];
    uint64_t t0 = mesh_rt_gtimer();
    uint32_t total = passes != 0 && tiles > UINT32_MAX / passes ? UINT32_MAX : tiles * passes;
    /* The item, as every thread sees it (uniform: the view's words before any write of this task). */
    uint64_t w = op == MESH_OP_TASK ? (uint64_t)arg : mesh_view_u64(v, PT_V_JOB_BASE) + arg;
    if (w >= total) return job ? MESH_R_JOB_FAIL : MESH_R_NONE;
    uint32_t t = (uint32_t)w % tiles, p = (uint32_t)w / tiles;
    mesh_shared part;
    if (!ptp_render_pass(ctx, sc, &l, t, p, &part)) return fail;
    if (mesh_ctx_warp(ctx) != 0) return MESH_R_NONE;
    uint32_t tm = mesh_view_u32(v, PT_V_TONEMAP);
    if (op == PT_OP_PASS) {
        /* A fleet job's item: the pass sums only, into the payload slot; no fold, no wait. */
        pt_v col[PT_TILE_H];
        uint64_t dp = ptp_fold(ptp_shared_col(part, mesh_ctx_lane(ctx), col));
        ptp_put_payload(ctx, v, col);
        if (mesh_ctx_leader(ctx)) {
            ptp_job_record(ctx, v, t, p, dp, 0, t0);
            ptp_bump(v, PT_V_ITEMS);
            mesh_ctx_count(ctx, 0, 1);
        }
        return (uint32_t)dp & 0x7fffffffu;
    }
    if (op == MESH_OP_JOB) {
        /* Item w = pass x tiles + tile: a lease's consecutive items are distinct
         * tiles, and pass p - 1 of a tile ran `tiles` items earlier. */
        uint64_t dp, da;
        if (!ptp_item(ctx, v, sc, part, t, p, tm, t0, true, &dp, &da)) return MESH_R_JOB_FAIL;
        if (mesh_ctx_leader(ctx)) ptp_job_record(ctx, v, t, p, dp, da, t0);
        /* The pass sums' digest: the same wherever and however often the item runs. */
        return (uint32_t)dp & 0x7fffffffu;
    }
    /* OP_TASK: pathtrace-live's work item arg (batches run in order, so the wait is nil). */
    uint32_t gen = mesh_view_u32(v, PT_V_GEN);
    uint64_t dp, da;
    bool done = ptp_item(ctx, v, sc, part, t, p, tm, t0, false, &dp, &da);
    if (mesh_ctx_leader(ctx) && mesh_view_u32(v, PT_V_GEN) == gen) ptp_bump(v, PT_V_NEXT);
    return done ? arg / tiles + 1 : MESH_R_BAD_OP;
}

static uint32_t ptp_dec(mesh_view resp, uint32_t len, uint32_t x) {
    uint8_t b[10];
    int i = 10;
    do {
        b[--i] = (uint8_t)('0' + x % 10);
        x /= 10;
    } while (x != 0);
    for (; i < 10; i++) mesh_view_set_u8(resp, len++, b[i]);
    return len;
}
static uint32_t ptp_str(mesh_view resp, uint32_t len, const char *s) {
    for (; *s; s++) mesh_view_set_u8(resp, len++, (uint8_t)*s);
    return len;
}

/* OP_REQ: the request window holds a scene's words (little-endian): loaded if
 * valid (then a restart); the response is one text line. */
static uint32_t ptp_request(const mesh_ctx *ctx, mesh_view v) {
    mesh_view req = mesh_ctx_request(ctx);
    uint32_t n = mesh_view_len(req) / 4;
    uint32_t m = n < PT_SCENE_MAX_WORDS ? n : PT_SCENE_MAX_WORDS;
    const uint32_t *sc = ptp_copy_words(ctx, req, 0, m);
    uint32_t err;
    if (mesh_view_len(req) % 4 != 0 || n > PT_SCENE_MAX_WORDS) err = 1;
    else if ((err = pt_validate(sc, m)) != 0) {}
    else if (sc[PT_H_WORDS] != n) err = 3;
    if (err == 0) {
        for (uint32_t i = mesh_ctx_lane(ctx); i < n; i += 32) mesh_view_set_u32(v, PT_V_SCENE + 4 * i, sc[i]);
        ptp_restart(ctx, v, sc);
    }
    mesh_view resp = mesh_ctx_response(ctx);
    uint32_t len = 0;
    if (mesh_ctx_leader(ctx)) {
        mesh_view_set_u32(v, PT_V_SCENE_ERR, err);
        if (err == 0) {
            len = ptp_str(resp, len, "ok scene gen ");
            len = ptp_dec(resp, len, mesh_view_u32(v, PT_V_GEN));
            len = ptp_str(resp, len, " tiles ");
            len = ptp_dec(resp, len, pt_tiles(sc));
        } else {
            len = ptp_str(resp, len, "err scene ");
            len = ptp_dec(resp, len, err);
        }
        len = ptp_str(resp, len, "\n");
    }
    mesh_ctx_respond(ctx, mesh_rt_shfl_idx(len, 0));
    return err == 0 ? MESH_R_NONE : MESH_R_BAD_OP;
}

static uint32_t ptp_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    /* A view without tiles (FLEET_VIEW_BYTES: pathtrace-cpu's) runs the fleet ops only. */
    bool fleet_op = op == MESH_OP_INIT || op == PT_OP_DEFAULT || op == PT_OP_SET || op == PT_OP_PASS;
    if (mesh_view_len(v) < PT_FLEET_VIEW_BYTES || (mesh_view_len(v) < PT_VIEW_BYTES && !fleet_op)) return MESH_R_BAD_OP;
    /* The render ops use the whole node; the rest run on warp 0 (thread 0's result is the task's). */
    if (op == MESH_OP_TASK || op == MESH_OP_JOB || op == PT_OP_PASS) return ptp_render(ctx, v, op, arg);
    if (mesh_ctx_warp(ctx) != 0) return MESH_R_NONE;
    switch (op) {
    case PT_OP_TONEMAP: break;
    case MESH_OP_INIT: ptp_init(ctx); return MESH_R_NONE;
    case PT_OP_DEFAULT: ptp_load_default(ctx, v); return MESH_R_NONE;
    case MESH_OP_REQ: return ptp_request(ctx, v);
    case PT_OP_RESTART: {
        const uint32_t *sc = ptp_scene(ctx, v, PT_MAX_TILES);
        if (!sc) return MESH_R_BAD_OP;
        ptp_restart(ctx, v, sc);
        return MESH_R_NONE;
    }
    case PT_OP_SET: {
        /* A job's setup: one scene word (no restart; OP_PASS validates the scene it renders). */
        bool settable = false;
        for (int i = 0; i < PT_SETTABLE_COUNT; i++) settable |= pt_settable[i] == (size_t)(arg >> 24);
        if (!settable) return MESH_R_BAD_OP;
        if (mesh_ctx_leader(ctx)) mesh_view_set_u32(v, PT_V_SCENE + 4 * (arg >> 24), arg & 0x00ffffffu);
        return MESH_R_NONE;
    }
    default: return MESH_R_BAD_OP;
    }
    /* OP_TONEMAP: re-tone every tile from its accumulators (warp 0). */
    const uint32_t *sc = ptp_scene(ctx, v, PT_MAX_TILES);
    if (!sc) return MESH_R_BAD_OP;
    uint32_t tiles = pt_tiles(sc);
    if (arg >= PT_TONEMAPS) return MESH_R_BAD_OP;
    if (mesh_ctx_leader(ctx)) mesh_view_set_u32(v, PT_V_TONEMAP, arg);
    for (uint32_t t = 0; t < tiles; t++) {
        uint32_t m = pt_view_meta(t);
        uint32_t done = mesh_view_u32(v, m + PT_M_PASSES) & ~PT_FOLDING;
        if (done == 0) continue;
        uint32_t cx = mesh_ctx_lane(ctx);
        pt_v acc[PT_TILE_H];
        uint32_t rgb[PT_TILE_H];
        uint64_t ha, hr;
        ptp_load_col(v, t, cx, acc);
        pt_retone_dev(sc, done, arg, acc, rgb, &ha, &hr);
        ptp_store_rgb(v, t, cx, rgb);
        hr = ptp_fold(hr);
        if (mesh_ctx_leader(ctx)) {
            mesh_view_set_u32(v, m + PT_M_RGB, (uint32_t)hr);
            mesh_view_set_u32(v, m + PT_M_RGB + 4, (uint32_t)(hr >> 32));
        }
    }
    if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, 3, 1);
    return MESH_R_NONE;
}

MESH_PROGRAM(ptp_init, ptp_task);
