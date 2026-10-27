/* @ajax/mesh-ref-doom: programs/doom's CPU reference (mesh-ref lib.rs), operation for operation as
 * the kernel computes in the cluster float mode: a column's DDA ray through the 16 x 16 map, the wall
 * height, and the player's pose moved by keys (the host pose of v1 with the view's turn constants).
 * Freestanding. */
#ifndef MESH_REF_DOOM_H
#define MESH_REF_DOOM_H

#include <stdbool.h>
#include <stdint.h>

/* Column c's ray from (px, py) along dir + plane cam: the wall kind hit (0: none within max_steps),
 * the side (0 x, 1 y) in *side, the perpendicular distance in *dist (1e30 for none). */
uint32_t mesh_doom_cast(const uint32_t map[256], float px, float py, float dx, float dy, float plx, float ply,
                        uint32_t c, uint32_t max_steps, uint32_t *side, float *dist);
/* The column height for a ray's (kind, dist), as doom's render: rows / dist, at most 4 rows. */
float mesh_doom_height(uint32_t kind, float dist);
/* The map as the view holds it: cell c = kind of (c % 16, c / 16). */
void mesh_doom_map(uint32_t out[256]);

/* The pose: x, y, dir x, dir y, plane x, plane y. */
typedef struct {
    float p[6];
} mesh_doom_pose;

/* A pose at the view's START. */
mesh_doom_pose mesh_doom_pose_start(void);
/* Inside the map and on an empty cell (FTZ compares; the cell by truncation). */
bool mesh_doom_walkable(float x, float y);
/* Moves by (dx, dy), each axis only if the spot RADIUS beyond it is walkable. */
void mesh_doom_pose_step(mesh_doom_pose *pose, float dx, float dy);
/* Rotates dir and plane by the angle with sine s (cosine TURN_COS). */
void mesh_doom_pose_rotate(mesh_doom_pose *pose, float s);
/* One key: w s forward/back, a d turn, q e strafe; anything else does nothing. */
void mesh_doom_pose_apply(mesh_doom_pose *pose, uint8_t key);

#endif
