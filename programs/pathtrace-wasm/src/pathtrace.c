/* pathtrace for the wasm executor (alpha-rust-mesh programs/pathtrace/wasm; programs/pathtrace has
 * the design): a node is one CPU thread, so a task renders all 32 columns of its tile in a loop --
 * pathtrace's tile in Z (@ajax/pathtrace-z, every f32 op flushed: the cluster float mode), the very
 * code of the CPU reference, so a CPU lane's tile is bit for bit a GPU's. The same ops, view
 * layout and results as the GPU images. */
#include "mesh/abi/ring.h"
#include "mesh/rt_wasm.h"
#include "pathtrace/default_scene.h"
#include "pathtrace/scene.h"
#include "pathtrace/view.h"
#include "pathtrace/z.h"

#define EXEC PT_EXEC_WASM
#define PIX PT_TILE_PIX

static uint32_t scene_buf[PT_SCENE_MAX_WORDS];
static pt_v part[PIX], acc[PIX];
static uint32_t words_buf[3 * PIX];
static uint32_t rgb[PIX];

static uint32_t clamp(uint32_t x, uint32_t lo, uint32_t hi) { return x < lo ? lo : x > hi ? hi : x; }

/* The view's scene (validated, at most max_tiles) into scene_buf: its words, or 0. */
static uint32_t scene(mesh_view v, uint32_t max_tiles) {
    uint32_t words = clamp(mesh_view_u32(v, PT_V_SCENE + 4 * PT_H_WORDS), PT_HEADER_WORDS, PT_SCENE_MAX_WORDS);
    mesh_view_read_u32s(v, PT_V_SCENE, scene_buf, words);
    return pt_validate_in(scene_buf, words, max_tiles) == 0 ? words : 0;
}

static void to_words(const pt_v *a) {
    for (uint32_t i = 0; i < PIX; i++) {
        words_buf[3 * i] = mesh_ftz_bits(a[i].x);
        words_buf[3 * i + 1] = mesh_ftz_bits(a[i].y);
        words_buf[3 * i + 2] = mesh_ftz_bits(a[i].z);
    }
}

/* The item's pass sums into its payload slot (ticket % 64), row-major r g b. */
static void put_payload(const mesh_ctx *ctx, mesh_view v) {
    to_words(part);
    mesh_view_write_u32s(v, pt_view_payload(mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS), words_buf, 3 * PIX);
}

static const uint32_t zeros[PT_MAX_TILES * PT_META / 4];

static void restart(mesh_view v, const uint32_t *sc) {
    uint32_t tiles = pt_tiles(sc);
    mesh_view_write_u32s(v, PT_V_META, zeros, PT_MAX_TILES * PT_META / 4);
    if (mesh_view_len(v) >= PT_VIEW_BYTES) {
        /* tiles x PIX zero words, a meta-sized block at a time. */
        uint32_t n = tiles * PIX, off = PT_V_RGB;
        const uint32_t chunk = PT_MAX_TILES * PT_META / 4;
        while (n > 0) {
            uint32_t k = n < chunk ? n : chunk;
            mesh_view_write_u32s(v, off, zeros, k);
            off += 4 * k;
            n -= k;
        }
    }
    const uint32_t offs[10] = {PT_V_NEXT, PT_V_DONE, PT_V_TILES, PT_V_WIDTH, PT_V_HEIGHT,
                               PT_V_SPP, PT_V_PASSES, PT_V_LAST_TILE, PT_V_LAST_SYSTEM, PT_V_LAST_EXEC};
    const uint32_t vals[10] = {0, 0, tiles, sc[PT_H_WIDTH], sc[PT_H_HEIGHT], sc[PT_H_SPP], sc[PT_H_PASSES], 0, 0, 0};
    for (int i = 0; i < 10; i++) mesh_view_set_u32(v, offs[i], vals[i]);
    mesh_view_set_u32(v, PT_V_GEN, mesh_view_u32(v, PT_V_GEN) + 1u);
    mesh_view_set_u32(v, PT_V_MAGIC, PT_VIEW_MAGIC);
}

static void load_default(mesh_view v) {
    mesh_view_write_u32s(v, PT_V_SCENE, pt_default_scene, PT_DEFAULT_SCENE_WORDS);
    restart(v, pt_default_scene);
}

static void init(const mesh_ctx *ctx) {
    mesh_view v = mesh_ctx_view(ctx);
    mesh_view_set_u32(v, PT_V_TONEMAP, PT_TONEMAP_ACES);
    mesh_view_set_u32(v, PT_V_SCENE_ERR, 0);
    load_default(v);
}

static void read_acc(mesh_view v, uint32_t t) {
    mesh_view_read_u32s(v, pt_view_acc(t), words_buf, 3 * PIX);
    for (uint32_t i = 0; i < PIX; i++) {
        acc[i].x = mesh_ftz_from_bits(words_buf[3 * i]);
        acc[i].y = mesh_ftz_from_bits(words_buf[3 * i + 1]);
        acc[i].z = mesh_ftz_from_bits(words_buf[3 * i + 2]);
    }
}

static void write_acc(mesh_view v, uint32_t t) {
    to_words(acc);
    mesh_view_write_u32s(v, pt_view_acc(t), words_buf, 3 * PIX);
}

/* How long an item waits for its tile's previous pass to be folded. */
#define WAIT_NS 2000000000ull

static void zero_v(pt_v *a) {
    for (uint32_t i = 0; i < PIX; i++) a[i].x = a[i].y = a[i].z = 0.0f;
}

/* Work item (tile t, pass p): the pass sums from +0, then, in pass order (M_PASSES claimed
 * p -> p | FOLDING, waiting bounded for passes 0..p), folded into the tile's accumulators and tone
 * mapped. *dp: the pass-sum digest; *da: the accumulator digest after the fold, or 0 when it was
 * folded already. false: the wait timed out. */
static bool item(const mesh_ctx *ctx, mesh_view v, const uint32_t *sc, uint32_t t, uint32_t p, uint32_t tm, uint64_t t0,
                 bool pay, uint64_t *dp, uint64_t *da) {
    zero_v(part);
    *dp = pt_tile_pass_z(sc, t, p, part);
    *da = 0;
    if (pay) put_payload(ctx, v);
    uint32_t m = pt_view_meta(t);
    for (;;) {
        uint32_t cur = mesh_view_u32(v, m + PT_M_PASSES);
        if (cur == p && mesh_view_cas32(v, m + PT_M_PASSES, p, p | PT_FOLDING) == p) break;
        if ((cur & ~PT_FOLDING) > p || (!(cur & PT_FOLDING) && cur > p)) return true;
        if (mesh_ctx_host_ns(ctx) - t0 > WAIT_NS) return false;
    }
    if (p > 0) read_acc(v, t);
    else zero_v(acc);
    uint64_t dr;
    pt_tile_fold_z(sc, p + 1, tm, acc, part, rgb, da, &dr);
    write_acc(v, t);
    mesh_view_write_u32s(v, pt_view_rgb(t), rgb, PIX);
    uint32_t exec = EXEC | mesh_ctx_node(ctx) << 8;
    uint32_t us = (uint32_t)((mesh_ctx_host_ns(ctx) - t0) / 1000);
    const uint32_t meta[7] = {mesh_ctx_system(ctx), exec, us, (uint32_t)*da, (uint32_t)(*da >> 32), (uint32_t)dr, (uint32_t)(dr >> 32)};
    mesh_view_write_u32s(v, m + 4, meta, 7);
    mesh_view_set_u32(v, PT_V_LAST_TILE, t);
    mesh_view_set_u32(v, PT_V_LAST_SYSTEM, mesh_ctx_system(ctx));
    mesh_view_set_u32(v, PT_V_LAST_EXEC, exec);
    mesh_view_cas32(v, m + PT_M_PASSES, p | PT_FOLDING, p + 1);
    mesh_view_add32(v, PT_V_ITEMS, 1);
    mesh_ctx_count(ctx, 0, 1);
    mesh_ctx_count(ctx, 2, 1);
    if (p + 1 == sc[PT_H_PASSES]) {
        mesh_ctx_count(ctx, 1, 1);
        mesh_view_add32(v, PT_V_DONE, 1);
    }
    return true;
}

/* OP_REQ: a scene's raw words: validated, loaded, restart; one text line back. */
static uint32_t request(const mesh_ctx *ctx, mesh_view v) {
    mesh_view req = mesh_ctx_request(ctx);
    uint32_t n = mesh_view_len(req) / 4;
    uint32_t k = n < PT_SCENE_MAX_WORDS ? n : PT_SCENE_MAX_WORDS;
    mesh_view_read_u32s(req, 0, scene_buf, k);
    uint32_t err;
    if (mesh_view_len(req) % 4 != 0 || n > PT_SCENE_MAX_WORDS) err = 1;
    else if ((err = pt_validate(scene_buf, k)) != 0) {
    } else if (scene_buf[PT_H_WORDS] != n) err = 3;
    else err = 0;
    if (err == 0) {
        mesh_view_write_u32s(v, PT_V_SCENE, scene_buf, k);
        restart(v, scene_buf);
    }
    mesh_view_set_u32(v, PT_V_SCENE_ERR, err);
    char line[64];
    uint32_t len = 0;
#define PUT(s)                                    \
    for (const char *q_ = (s); *q_; q_++) line[len++] = *q_
#define NUM(x)                                    \
    do {                                          \
        char d_[12];                              \
        int i_ = 0;                               \
        uint32_t y_ = (x);                        \
        do {                                      \
            d_[i_++] = (char)('0' + y_ % 10);     \
            y_ /= 10;                             \
        } while (y_);                             \
        while (i_) line[len++] = d_[--i_];        \
    } while (0)
    if (err == 0) {
        PUT("ok scene gen ");
        NUM(mesh_view_u32(v, PT_V_GEN));
        PUT(" tiles ");
        NUM(pt_tiles(scene_buf));
        PUT("\n");
    } else {
        PUT("err scene ");
        NUM(err);
        PUT("\n");
    }
    mesh_view_write(mesh_ctx_response(ctx), 0, line, len);
    mesh_ctx_respond(ctx, len);
    return err == 0 ? MESH_R_NONE : MESH_R_BAD_OP;
}

static bool settable(uint32_t word) {
    for (int i = 0; i < PT_SETTABLE_COUNT; i++)
        if (pt_settable[i] == word) return true;
    return false;
}

static uint32_t task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    /* A view without tiles (FLEET_VIEW_BYTES: pathtrace-cpu's) runs the fleet ops only. */
    uint32_t len = mesh_view_len(v);
    if (len < PT_FLEET_VIEW_BYTES ||
        (len < PT_VIEW_BYTES && !(op == MESH_OP_INIT || op == PT_OP_DEFAULT || op == PT_OP_SET || op == PT_OP_PASS)))
        return MESH_R_BAD_OP;
    switch (op) {
    case MESH_OP_TASK:
    case MESH_OP_JOB:
    case PT_OP_TONEMAP:
    case PT_OP_PASS: break;
    case MESH_OP_INIT: init(ctx); return MESH_R_NONE;
    case PT_OP_DEFAULT: load_default(v); return MESH_R_NONE;
    case MESH_OP_REQ: return request(ctx, v);
    case PT_OP_RESTART:
        if (!scene(v, PT_MAX_TILES)) return MESH_R_BAD_OP;
        restart(v, scene_buf);
        return MESH_R_NONE;
    case PT_OP_SET:
        if (!settable(arg >> 24)) return MESH_R_BAD_OP;
        mesh_view_set_u32(v, PT_V_SCENE + 4 * (arg >> 24), arg & 0x00ffffffu);
        return MESH_R_NONE;
    default: return MESH_R_BAD_OP;
    }
    bool job = op == MESH_OP_JOB || op == PT_OP_PASS;
    if (!scene(v, op == PT_OP_PASS ? PT_FLEET_MAX_TILES : PT_MAX_TILES)) return job ? MESH_R_JOB_FAIL : MESH_R_BAD_OP;
    const uint32_t *sc = scene_buf;
    uint32_t tiles = pt_tiles(sc), passes = sc[PT_H_PASSES];
    uint64_t t0 = mesh_ctx_host_ns(ctx);
    if (op == PT_OP_TONEMAP) {
        if (arg >= PT_TONEMAPS) return MESH_R_BAD_OP;
        mesh_view_set_u32(v, PT_V_TONEMAP, arg);
        for (uint32_t t = 0; t < tiles; t++) {
            uint32_t m = pt_view_meta(t);
            uint32_t done = mesh_view_u32(v, m + PT_M_PASSES) & ~PT_FOLDING;
            if (done == 0) continue;
            read_acc(v, t);
            uint64_t hr = pt_tile_retone_z(sc, done, arg, acc, rgb);
            mesh_view_write_u32s(v, pt_view_rgb(t), rgb, PIX);
            const uint32_t w[2] = {(uint32_t)hr, (uint32_t)(hr >> 32)};
            mesh_view_write_u32s(v, m + PT_M_RGB, w, 2);
        }
        mesh_ctx_count(ctx, 3, 1);
        return MESH_R_NONE;
    }
    uint32_t tm = mesh_view_u32(v, PT_V_TONEMAP);
    uint64_t total64 = (uint64_t)tiles * passes;
    uint32_t total = total64 > UINT32_MAX ? UINT32_MAX : (uint32_t)total64;
    if (op == PT_OP_PASS) {
        /* A fleet job's item: the pass sums only, into the payload slot; no fold, no wait. */
        uint64_t w = mesh_view_u64(v, PT_V_JOB_BASE) + arg;
        if (w >= total) return MESH_R_JOB_FAIL;
        uint32_t t = (uint32_t)w % tiles, p = (uint32_t)w / tiles;
        zero_v(part);
        uint64_t dp = pt_tile_pass_z(sc, t, p, part);
        put_payload(ctx, v);
        uint32_t us = (uint32_t)((mesh_ctx_host_ns(ctx) - t0) / 1000);
        const uint32_t rec[8] = {t, p, (uint32_t)dp, (uint32_t)(dp >> 32), 0, 0, us, EXEC | mesh_ctx_node(ctx) << 8};
        mesh_view_write_u32s(v, PT_V_JOB_OUT + PT_JOB_REC * (mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS), rec, 8);
        mesh_view_add32(v, PT_V_ITEMS, 1);
        mesh_ctx_count(ctx, 0, 1);
        return (uint32_t)dp & 0x7fffffffu;
    }
    if (op == MESH_OP_JOB) {
        uint64_t w = mesh_view_u64(v, PT_V_JOB_BASE) + arg;
        if (w >= total) return MESH_R_JOB_FAIL;
        uint32_t t = (uint32_t)w % tiles, p = (uint32_t)w / tiles;
        uint64_t dp, da;
        if (!item(ctx, v, sc, t, p, tm, t0, true, &dp, &da)) return MESH_R_JOB_FAIL;
        uint32_t us = (uint32_t)((mesh_ctx_host_ns(ctx) - t0) / 1000);
        const uint32_t rec[8] = {t, p, (uint32_t)dp, (uint32_t)(dp >> 32), (uint32_t)da, (uint32_t)(da >> 32), us,
                                 EXEC | mesh_ctx_node(ctx) << 8};
        mesh_view_write_u32s(v, PT_V_JOB_OUT + PT_JOB_REC * (mesh_ctx_ticket(ctx) % MESH_PROG_RESULTS), rec, 8);
        return (uint32_t)dp & 0x7fffffffu;
    }
    if (arg >= total) return MESH_R_NONE;
    uint32_t gen = mesh_view_u32(v, PT_V_GEN);
    uint64_t dp, da;
    bool done = item(ctx, v, sc, arg % tiles, arg / tiles, tm, t0, false, &dp, &da);
    if (mesh_view_u32(v, PT_V_GEN) == gen) mesh_view_add32(v, PT_V_NEXT, 1);
    return done ? arg / tiles + 1 : MESH_R_BAD_OP;
}

MESH_PROGRAM(init, task)
