/* @ajax/mesh-abi-view-doom: programs/doom's view: a DDA raycaster's pose, map, columns and show region.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_DOOM_H
#define MESH_ABI_VIEW_DOOM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/placement.h"

/* A view region, (offset, bytes): PLACE_STATE's and PLACE_OUT's entries. */
typedef struct {
    uint32_t off;
    uint32_t len;
} mesh_doom_region;

/* programs/doom: a DDA raycaster, one lane per column, 4 tasks a frame
 * (arg b: columns 32 b .. 32 b + 31).  The pose lives in the view: the
 * frame's arg-0 task drains the input queue (keys w s a d q e) into it, with
 * wall collision, then publishes D_POSE_T = its ticket + 1; args 1..3 wait for
 * it (tickets of one batch are consecutive), at most POSE_WAIT_NS, then render
 * (a timeout bumps counter C_STALLS). */
#define MESH_DOOM_WIDTH 128u
#define MESH_DOOM_D_POS_X 0x00u /* f32 x 6: pos x y, dir x y, plane x y */
#define MESH_DOOM_D_POS_Y 0x04u
#define MESH_DOOM_D_DIR_X 0x08u
#define MESH_DOOM_D_DIR_Y 0x0cu
#define MESH_DOOM_D_PLANE_X 0x10u
#define MESH_DOOM_D_PLANE_Y 0x14u
#define MESH_DOOM_D_ROWS 0x18u /* f32 screen rows (heights are in rows) */
#define MESH_DOOM_D_MAX_STEPS 0x1cu /* u32 DDA steps at most */
#define MESH_DOOM_D_FRAME 0x20u /* u32 frames begun (arg-0 tasks) */
#define MESH_DOOM_D_POSE_T 0x24u /* u32 ticket + 1 of the last pose update */
#define MESH_DOOM_D_KEYS 0x28u /* u32 keys applied */
#define MESH_DOOM_D_MAP 0x100u /* u32 x 256, row-major 16x16, 0 empty else kind 1..7 */
#define MESH_DOOM_D_HEIGHT 0x1000u /* f32 x WIDTH */
#define MESH_DOOM_D_KIND 0x1400u /* u32 x WIDTH */
#define MESH_DOOM_D_SIDE 0x1800u /* u32 x WIDTH */
#define MESH_DOOM_D_DIST 0x1c00u /* f32 x WIDTH */
/* Placement (manifest `place = "split"`, mesh-abi v2 "placement"): the
 * state meshd moves between the machines is the pose, rows, max steps
 * and frame (0x0..0x24), D_KEYS and the map; D_POSE_T stays each
 * machine's own (it holds that machine's tickets).  The output is the
 * four column arrays, task arg b writing columns 32 b .. 32 b + 31 of
 * each.  meshd's frame-matched copy is at D_SHOW (SH_*): buffer k holds
 * the arrays concatenated, from show_data(k): heights, kinds, sides, dists. */
#define MESH_DOOM_PLACE_STATE_LEN 3
extern const mesh_doom_region MESH_DOOM_PLACE_STATE[3];
#define MESH_DOOM_PLACE_OUT_LEN 4
extern const mesh_doom_region MESH_DOOM_PLACE_OUT[4];
#define MESH_DOOM_OUT_BYTES ((uint32_t)(16 * MESH_DOOM_WIDTH))
#define MESH_DOOM_D_SHOW 0x2000u
#define MESH_DOOM_SHOW_HEIGHT 0u
#define MESH_DOOM_SHOW_KIND ((uint32_t)(4 * MESH_DOOM_WIDTH))
#define MESH_DOOM_SHOW_SIDE ((uint32_t)(8 * MESH_DOOM_WIDTH))
#define MESH_DOOM_SHOW_DIST ((uint32_t)(12 * MESH_DOOM_WIDTH))
/* The view; the input queue (manifest `input = 64`) sits at its end. */
#define MESH_DOOM_VIEW_BYTES 0x4000u
/* The snapshot prefix: everything up to the end of the show region. */
#define MESH_DOOM_SNAP_BYTES ((uint32_t)(MESH_DOOM_D_SHOW + MESH_SH_DATA + 2 * MESH_DOOM_OUT_BYTES)) /* = D_SHOW + show_bytes(OUT_BYTES) */
#define MESH_DOOM_INPUT_BYTES 64u
#define MESH_DOOM_ROWS 48.0f
#define MESH_DOOM_MAX_STEPS 64u
#define MESH_DOOM_POSE_WAIT_NS UINT64_C(100000000)
#define MESH_DOOM_KEYS "wsadqe"
#define MESH_DOOM_KEYS_LEN 6
/* Movement: MOVE cells a key, turns of 10 degrees (cos, sin as f32
 * constants, so the GPU and a CPU reference agree bitwise), a ~66 degree
 * field of view, and RADIUS kept from walls. */
#define MESH_DOOM_MOVE 0.25f
#define MESH_DOOM_TURN_COS 0.9848077f /* cos(10 deg), correctly rounded */
#define MESH_DOOM_TURN_SIN 0.17364818f /* sin(10 deg) */
#define MESH_DOOM_PLANE 0.66f
#define MESH_DOOM_RADIUS 0.2f
/* The start pose: the open hall in the lower half, looking up (-y). */
#define MESH_DOOM_START_LEN 6
extern const float MESH_DOOM_START[6];
/* 16x16, row-major (y rows, x columns); digits are wall kinds 1..7, '.' empty. */
#define MESH_DOOM_MAP_LEN 16
/* rows of 16 cells; [17] leaves room for the NUL of C strings */
extern const char MESH_DOOM_MAP[16][17];
/* Counters: 0 frames, 1 keys, 2 stalls (a pose wait timed out), 3
 * follows (OP_FOLLOW parts rendered on the owner's pose: split, system 1). */
#define MESH_DOOM_C_FRAMES 0u
#define MESH_DOOM_C_KEYS 1u
#define MESH_DOOM_C_STALLS 2u
#define MESH_DOOM_C_FOLLOWS 3u

/* Buffer k's data in the view. */
static inline uint32_t mesh_doom_show_data(uint32_t k) { return MESH_DOOM_D_SHOW + (uint32_t)MESH_SH_DATA + k * MESH_DOOM_OUT_BYTES; }
/* MAP's cell (x, y): its wall kind 1..9, or 0 when empty. */
static inline uint32_t mesh_doom_map_cell(size_t x, size_t y) {
    char c = MESH_DOOM_MAP[y][x];
    return c >= '1' && c <= '9' ? (uint32_t)(c - '0') : 0;
}

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_DOOM_D_DIST + 4 * MESH_DOOM_WIDTH + 0x10 + MESH_DOOM_INPUT_BYTES <= MESH_DOOM_VIEW_BYTES, "doom::D_DIST + 4 * doom::WIDTH + 0x10 + doom::INPUT_BYTES <= doom::VIEW_BYTES");
_Static_assert(MESH_DOOM_D_MAP + 4 * 256 <= MESH_DOOM_D_HEIGHT, "doom::D_MAP + 4 * 256 <= doom::D_HEIGHT");
_Static_assert(MESH_DOOM_D_KEYS + 4 <= MESH_DOOM_D_MAP, "doom::D_KEYS + 4 <= doom::D_MAP");
_Static_assert((MESH_DOOM_WIDTH % 32 == 0), "doom::WIDTH.is_multiple_of(32)");
_Static_assert(MESH_DOOM_D_DIST + 4 * MESH_DOOM_WIDTH <= MESH_DOOM_D_SHOW, "doom::D_DIST + 4 * doom::WIDTH <= doom::D_SHOW");
_Static_assert(MESH_DOOM_SNAP_BYTES == 0x3100, "doom::SNAP_BYTES == 0x3100");
_Static_assert(MESH_DOOM_SNAP_BYTES + 0x10 + MESH_DOOM_INPUT_BYTES <= MESH_DOOM_VIEW_BYTES, "doom::SNAP_BYTES + 0x10 + doom::INPUT_BYTES <= doom::VIEW_BYTES");

#endif
