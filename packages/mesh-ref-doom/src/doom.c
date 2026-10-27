/* programs/doom's CPU reference (mesh-ref lib.rs `cast`, `height`, `doom_map`, `Pose`). */
#include "mesh/ref/doom.h"

#include "mesh/abi/view_doom.h"
#include "mesh/ftz.h"

/* f32 -> i32 as Rust's `as` and cvt.rzi: truncation, saturating, NaN to 0. */
static int32_t to_i32(float x) {
    if (mesh_ftz_is_nan(x)) return 0;
    if (x >= 2147483648.0f) return INT32_MAX;
    if (x <= -2147483648.0f) return INT32_MIN;
    return (int32_t)x;
}

uint32_t mesh_doom_cast(const uint32_t map[256], float px, float py, float dx, float dy, float plx, float ply,
                        uint32_t c, uint32_t max_steps, uint32_t *side_out, float *dist) {
    float cam = mesh_ftz_sub(mesh_ftz_div(mesh_ftz_mul(2.0f, mesh_ftz_from_u32(c)), mesh_ftz_from_u32(MESH_DOOM_WIDTH)), 1.0f);
    float rx = mesh_ftz_add(dx, mesh_ftz_mul(plx, cam));
    float ry = mesh_ftz_add(dy, mesh_ftz_mul(ply, cam));
    /* cvt.rzi.ftz.s32.f32: a subnormal truncates to 0 flushed or not. */
    int32_t mx = to_i32(px), my = to_i32(py);
    float ddx = mesh_ftz_eq(rx, 0.0f) ? 1e30f : mesh_ftz_abs(mesh_ftz_div(1.0f, rx));
    float ddy = mesh_ftz_eq(ry, 0.0f) ? 1e30f : mesh_ftz_abs(mesh_ftz_div(1.0f, ry));
    int32_t sx, sy;
    float sdx, sdy;
    if (mesh_ftz_lt(rx, 0.0f))
        sx = -1, sdx = mesh_ftz_mul(mesh_ftz_sub(px, mesh_ftz_from_i32(mx)), ddx);
    else
        sx = 1, sdx = mesh_ftz_mul(mesh_ftz_sub(mesh_ftz_add(mesh_ftz_from_i32(mx), 1.0f), px), ddx);
    if (mesh_ftz_lt(ry, 0.0f))
        sy = -1, sdy = mesh_ftz_mul(mesh_ftz_sub(py, mesh_ftz_from_i32(my)), ddy);
    else
        sy = 1, sdy = mesh_ftz_mul(mesh_ftz_sub(mesh_ftz_add(mesh_ftz_from_i32(my), 1.0f), py), ddy);
    uint32_t side = 0;
    for (uint32_t i = 0; i < max_steps; i++) {
        if (mesh_ftz_lt(sdx, sdy)) {
            sdx = mesh_ftz_add(sdx, ddx);
            mx += sx;
            side = 0;
        } else {
            sdy = mesh_ftz_add(sdy, ddy);
            my += sy;
            side = 1;
        }
        if (mx < 0 || mx >= 16 || my < 0 || my >= 16) break;
        uint32_t k = map[my * 16 + mx] & 15;
        if (k != 0) {
            *side_out = side;
            *dist = side == 0 ? mesh_ftz_sub(sdx, ddx) : mesh_ftz_sub(sdy, ddy);
            return k;
        }
    }
    *side_out = side;
    *dist = 1e30f;
    return 0;
}

float mesh_doom_height(uint32_t kind, float dist) {
    float rows = MESH_DOOM_ROWS;
    if (kind == 0) return 0.0f;
    float h = mesh_ftz_div(rows, dist);
    float cap = mesh_ftz_mul(4.0f, rows);
    return mesh_ftz_gt(h, cap) || mesh_ftz_is_nan(h) ? cap : h;
}

void mesh_doom_map(uint32_t out[256]) {
    for (uint32_t c = 0; c < 256; c++) out[c] = mesh_doom_map_cell(c % 16, c / 16);
}

mesh_doom_pose mesh_doom_pose_start(void) {
    mesh_doom_pose pose;
    for (int i = 0; i < 6; i++) pose.p[i] = MESH_DOOM_START[i];
    return pose;
}

bool mesh_doom_walkable(float x, float y) {
    return mesh_ftz_ge(x, 0.0f) && mesh_ftz_ge(y, 0.0f) && mesh_ftz_lt(x, 16.0f) && mesh_ftz_lt(y, 16.0f) &&
           mesh_doom_map_cell((size_t)to_i32(x), (size_t)to_i32(y)) == 0;
}

/* The radius's sign is the sign bit (copysign: no flush), as doom's `sign`. */
static float sign(float v) { return (mesh_ftz_bits(v) >> 31) != 0 ? -1.0f : 1.0f; }

void mesh_doom_pose_step(mesh_doom_pose *pose, float dx, float dy) {
    float x = pose->p[0], y = pose->p[1];
    if (mesh_doom_walkable(mesh_ftz_add(mesh_ftz_add(x, dx), mesh_ftz_mul(MESH_DOOM_RADIUS, sign(dx))), y))
        pose->p[0] = mesh_ftz_add(x, dx);
    x = pose->p[0];
    if (mesh_doom_walkable(x, mesh_ftz_add(mesh_ftz_add(y, dy), mesh_ftz_mul(MESH_DOOM_RADIUS, sign(dy)))))
        pose->p[1] = mesh_ftz_add(y, dy);
}

void mesh_doom_pose_rotate(mesh_doom_pose *pose, float s) {
    float c = MESH_DOOM_TURN_COS;
    for (int i = 2; i <= 4; i += 2) {
        float x = pose->p[i], y = pose->p[i + 1];
        pose->p[i] = mesh_ftz_sub(mesh_ftz_mul(x, c), mesh_ftz_mul(y, s));
        pose->p[i + 1] = mesh_ftz_add(mesh_ftz_mul(x, s), mesh_ftz_mul(y, c));
    }
}

void mesh_doom_pose_apply(mesh_doom_pose *pose, uint8_t key) {
    float dx = mesh_ftz_mul(pose->p[2], MESH_DOOM_MOVE), dy = mesh_ftz_mul(pose->p[3], MESH_DOOM_MOVE);
    float px = pose->p[4], py = pose->p[5];
    switch (key) {
    case 'w': mesh_doom_pose_step(pose, dx, dy); break;
    case 's': mesh_doom_pose_step(pose, mesh_ftz_neg(dx), mesh_ftz_neg(dy)); break;
    case 'a': mesh_doom_pose_rotate(pose, mesh_ftz_neg(MESH_DOOM_TURN_SIN)); break;
    case 'd': mesh_doom_pose_rotate(pose, MESH_DOOM_TURN_SIN); break;
    case 'q':
        mesh_doom_pose_step(pose, mesh_ftz_mul(mesh_ftz_div(mesh_ftz_neg(px), MESH_DOOM_PLANE), MESH_DOOM_MOVE),
                            mesh_ftz_mul(mesh_ftz_div(mesh_ftz_neg(py), MESH_DOOM_PLANE), MESH_DOOM_MOVE));
        break;
    case 'e':
        mesh_doom_pose_step(pose, mesh_ftz_mul(mesh_ftz_div(px, MESH_DOOM_PLANE), MESH_DOOM_MOVE),
                            mesh_ftz_mul(mesh_ftz_div(py, MESH_DOOM_PLANE), MESH_DOOM_MOVE));
        break;
    default: break;
    }
}
