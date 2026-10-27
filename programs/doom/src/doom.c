/* doom: a DDA raycaster (Lode Vandevenne's), one lane per screen column, 4
 * tasks a frame (arg b: columns 32 b .. 32 b + 31). The pose lives in the
 * view: the frame's arg-0 task drains the input queue (w s a d q e) into it,
 * with wall collision, and publishes D_POSE_T = ticket + 1; the frame's other
 * tasks (tickets t0 + k, consecutive within a publish) wait for it, bounded.
 * The map comes from OP_INIT. Layout: @ajax/mesh-abi-view-doom. Split across
 * machines (manifest `place = "split"`), every system but the owner renders
 * its columns with OP_FOLLOW: meshd wrote the owner's pose for that frame into
 * the view beforehand, so it neither waits nor reads input.
 * v1's raycaster, kept as written: it mirrors the CPU reference (`h == h` is the NaN test).
 * Ported from alpha-rust-mesh programs/doom/src/lib.rs. */
#include "mesh/rt.h"

static float doom_fabs(float x) { return mesh_f32_from_bits(mesh_f32_bits(x) & 0x7fffffffu); }
/* f32 signum for non-NaN x (+0.0 gives 1.0, as the host Pose did). */
static float doom_sign(float x) { return (mesh_f32_bits(x) >> 31) ? -1.0f : 1.0f; }

static void doom_init(const mesh_ctx *ctx) {
    for (uint32_t c = mesh_ctx_lane(ctx); c < 256; c += 32) mesh_ctx_set_u32(ctx, MESH_DOOM_D_MAP + 4 * c, mesh_doom_map_cell(c % 16, c / 16));
    if (mesh_ctx_leader(ctx)) {
        for (uint32_t i = 0; i < MESH_DOOM_START_LEN; i++) mesh_ctx_set_f32(ctx, MESH_DOOM_D_POS_X + 4 * i, MESH_DOOM_START[i]);
        mesh_ctx_set_f32(ctx, MESH_DOOM_D_ROWS, MESH_DOOM_ROWS);
        mesh_ctx_set_u32(ctx, MESH_DOOM_D_MAX_STEPS, MESH_DOOM_MAX_STEPS);
        mesh_ctx_set_u32(ctx, MESH_DOOM_D_FRAME, 0);
        mesh_ctx_set_u32(ctx, MESH_DOOM_D_POSE_T, 0);
        mesh_ctx_set_u32(ctx, MESH_DOOM_D_KEYS, 0);
    }
}

/* The pose: x y, dir x y, plane x y. */
typedef struct {
    float p[6];
} doom_pose;

static doom_pose doom_pose_read(mesh_view v) {
    doom_pose o;
    for (uint32_t i = 0; i < 6; i++) o.p[i] = mesh_view_f32(v, MESH_DOOM_D_POS_X + 4 * i);
    return o;
}
static void doom_pose_write(const doom_pose *o, mesh_view v) {
    for (uint32_t i = 0; i < 6; i++) mesh_view_set_f32(v, MESH_DOOM_D_POS_X + 4 * i, o->p[i]);
}
static bool doom_walkable(mesh_view v, float x, float y) {
    return x >= 0.0f && y >= 0.0f && x < 16.0f && y < 16.0f &&
           (mesh_view_u32(v, MESH_DOOM_D_MAP + 4 * (16 * (uint32_t)y + (uint32_t)x)) & 15u) == 0;
}
/* Moves by (dx, dy), each axis separately so the player slides along walls,
 * keeping RADIUS from them. */
static void doom_step(doom_pose *o, mesh_view v, float dx, float dy) {
    if (doom_walkable(v, o->p[0] + dx + MESH_DOOM_RADIUS * doom_sign(dx), o->p[1])) o->p[0] += dx;
    if (doom_walkable(v, o->p[0], o->p[1] + dy + MESH_DOOM_RADIUS * doom_sign(dy))) o->p[1] += dy;
}
static void doom_rotate(doom_pose *o, float s) {
    float c = MESH_DOOM_TURN_COS;
    float x = o->p[2], y = o->p[3];
    o->p[2] = x * c - y * s;
    o->p[3] = x * s + y * c;
    x = o->p[4], y = o->p[5];
    o->p[4] = x * c - y * s;
    o->p[5] = x * s + y * c;
}
/* w/s forward/back, a/d turn left/right, q/e strafe (y points down the map). */
static void doom_apply(doom_pose *o, mesh_view v, uint8_t key) {
    float dx = o->p[2] * MESH_DOOM_MOVE, dy = o->p[3] * MESH_DOOM_MOVE;
    float px = o->p[4], py = o->p[5];
    switch (key) {
    case 'w': doom_step(o, v, dx, dy); break;
    case 's': doom_step(o, v, -dx, -dy); break;
    case 'a': doom_rotate(o, -MESH_DOOM_TURN_SIN); break;
    case 'd': doom_rotate(o, MESH_DOOM_TURN_SIN); break;
    /* The camera plane points to the player's right. */
    case 'q': doom_step(o, v, -px / MESH_DOOM_PLANE * MESH_DOOM_MOVE, -py / MESH_DOOM_PLANE * MESH_DOOM_MOVE); break;
    case 'e': doom_step(o, v, px / MESH_DOOM_PLANE * MESH_DOOM_MOVE, py / MESH_DOOM_PLANE * MESH_DOOM_MOVE); break;
    default: break;
    }
}

/* One ray: the wall kind or 0, *side 0 x / 1 y, *dist the perpendicular distance or 1e30. */
static inline __attribute__((always_inline)) uint32_t doom_cast(const uint32_t map[32], float px, float py, float dx, float dy,
                                                                 float plx, float ply, uint32_t c, uint32_t max_steps,
                                                                 uint32_t *side_out, float *dist_out) {
    float cam = 2.0f * (float)c / (float)MESH_DOOM_WIDTH - 1.0f;
    float rx = dx + plx * cam;
    float ry = dy + ply * cam;
    int32_t mx = (int32_t)px, my = (int32_t)py;
    float ddx = rx == 0.0f ? 1e30f : doom_fabs(1.0f / rx);
    float ddy = ry == 0.0f ? 1e30f : doom_fabs(1.0f / ry);
    int32_t sx, sy;
    float sdx, sdy;
    if (rx < 0.0f) { sx = -1; sdx = (px - (float)mx) * ddx; }
    else { sx = 1; sdx = ((float)mx + 1.0f - px) * ddx; }
    if (ry < 0.0f) { sy = -1; sdy = (py - (float)my) * ddy; }
    else { sy = 1; sdy = ((float)my + 1.0f - py) * ddy; }
    uint32_t side = 0;
    for (uint32_t i = 0; i < max_steps; i++) {
        if (sdx < sdy) { sdx += ddx; mx += sx; side = 0; }
        else { sdy += ddy; my += sy; side = 1; }
        if (mx < 0 || mx > 15 || my < 0 || my > 15) break;
        uint32_t cell = (uint32_t)(my * 16 + mx);
        uint32_t k = (map[cell >> 3] >> (4 * (cell & 7))) & 15u;
        if (k != 0) {
            *side_out = side;
            *dist_out = side == 0 ? sdx - ddx : sdy - ddy;
            return k;
        }
    }
    *side_out = side;
    *dist_out = 1e30f;
    return 0;
}

/* Renders columns 32 b .. 32 b + 31; returns how many hit a wall. */
static uint32_t doom_render(const mesh_ctx *ctx, mesh_view v, const doom_pose *pose, uint32_t b) {
    uint32_t l = mesh_ctx_lane(ctx);
    /* The 16x16 map, 4 bits a cell: lane l packs cells 8l..8l+7, then every lane
     * gathers all 32 words so the DDA loop never touches host memory. */
    uint32_t packed = 0;
    for (uint32_t j = 0; j < 8; j++) packed |= (mesh_view_u32(v, MESH_DOOM_D_MAP + 4 * (8 * l + j)) & 15u) << (4 * j);
    uint32_t map[32];
    for (uint32_t w = 0; w < 32; w++) map[w] = mesh_rt_shfl_idx(packed, w);

    uint32_t c = b * 32 + l;
    bool hit = false;
    if (c < MESH_DOOM_WIDTH) {
        float rows = mesh_view_f32(v, MESH_DOOM_D_ROWS);
        uint32_t side;
        float dist;
        uint32_t kind = doom_cast(map, pose->p[0], pose->p[1], pose->p[2], pose->p[3], pose->p[4], pose->p[5], c,
                                  mesh_view_u32(v, MESH_DOOM_D_MAX_STEPS), &side, &dist);
        hit = kind != 0;
        float height = 0.0f;
        if (hit) {
            float h = rows / dist;
            height = (h > 4.0f * rows || !(h == h)) ? 4.0f * rows : h;
        }
        mesh_view_set_f32(v, MESH_DOOM_D_HEIGHT + 4 * c, height);
        mesh_view_set_u32(v, MESH_DOOM_D_KIND + 4 * c, kind);
        mesh_view_set_u32(v, MESH_DOOM_D_SIDE + 4 * c, side);
        mesh_view_set_f32(v, MESH_DOOM_D_DIST + 4 * c, dist);
    }
    return (uint32_t)__builtin_popcount(mesh_rt_ballot(hit));
}

/* Arg 0: apply the queued keys (all lanes pop collectively and compute the
 * same pose; at most one queue's worth a frame), then release the pose. */
static doom_pose doom_begin_frame(const mesh_ctx *ctx, mesh_view v) {
    doom_pose pose = doom_pose_read(v);
    uint32_t n = 0;
    while (n < MESH_DOOM_INPUT_BYTES) {
        uint8_t k;
        if (!mesh_ctx_input_pop(ctx, &k)) break;
        doom_apply(&pose, v, k);
        n++;
    }
    if (mesh_ctx_leader(ctx)) {
        doom_pose_write(&pose, v);
        mesh_view_set_u32(v, MESH_DOOM_D_FRAME, mesh_view_u32(v, MESH_DOOM_D_FRAME) + 1);
        mesh_view_set_u32(v, MESH_DOOM_D_KEYS, mesh_view_u32(v, MESH_DOOM_D_KEYS) + n);
        mesh_ctx_count(ctx, MESH_DOOM_C_FRAMES, 1);
        if (n > 0) mesh_ctx_count(ctx, MESH_DOOM_C_KEYS, n);
        mesh_rt_fence_acq_rel();
        mesh_view w;
        if (mesh_view_sub(v, MESH_DOOM_D_POSE_T, 4, &w)) mesh_rt_st_release(mesh_view_ptr(w), mesh_ctx_ticket(ctx) + 1);
    }
    mesh_rt_warp_sync();
    return pose;
}

/* Args k > 0: wait until the arg-0 task of this publish (ticket t - k) has
 * released the pose; a stray or lost arg-0 costs at most POSE_WAIT_NS and
 * bumps C_STALLS. */
static doom_pose doom_wait_pose(const mesh_ctx *ctx, mesh_view v, uint32_t k) {
    if (mesh_ctx_leader(ctx)) {
        mesh_view w;
        if (mesh_view_sub(v, MESH_DOOM_D_POSE_T, 4, &w)) {
            uint32_t want = mesh_ctx_ticket(ctx) + 1;
            uint64_t t0 = mesh_rt_gtimer();
            /* The arg-0 ticket lies in [t - k, t]: want - pose_t in [0, k]. */
            while (want - mesh_rt_ld_acquire(mesh_view_ptr(w)) > k) {
                if (mesh_rt_gtimer() - t0 > MESH_DOOM_POSE_WAIT_NS) {
                    mesh_ctx_count(ctx, MESH_DOOM_C_STALLS, 1);
                    break;
                }
                mesh_rt_nanosleep(200);
            }
        }
    }
    /* Lane 0's acquire, then the warp barrier, orders every lane's pose reads. */
    mesh_rt_warp_sync();
    return doom_pose_read(v);
}

static uint32_t doom_task(const mesh_ctx *ctx, uint32_t op, uint32_t arg) {
    mesh_view v = mesh_ctx_view(ctx);
    if (op == MESH_OP_FOLLOW && arg < MESH_DOOM_WIDTH / 32) {
        if (mesh_ctx_leader(ctx)) mesh_ctx_count(ctx, MESH_DOOM_C_FOLLOWS, 1);
        doom_pose pose = doom_pose_read(v);
        return doom_render(ctx, v, &pose, arg);
    }
    if (op != MESH_OP_TASK) return MESH_R_BAD_OP;
    doom_pose pose = arg == 0 ? doom_begin_frame(ctx, v) : doom_wait_pose(ctx, v, arg);
    return doom_render(ctx, v, &pose, arg);
}

MESH_PROGRAM(doom_init, doom_task);
