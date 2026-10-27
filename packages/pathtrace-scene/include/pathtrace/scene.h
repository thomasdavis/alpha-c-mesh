/* @ajax/pathtrace-scene: programs/pathtrace's scene format (pathtrace-core lib.rs): a scene is u32
 * words (floats as their bits): a 32-word header, then materials, quads, spheres and triangles. Also
 * the image's tiles (32 x 24 pixels) and the scene words a fleet job may set. Freestanding. */
#ifndef PATHTRACE_SCENE_H
#define PATHTRACE_SCENE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PT_MAGIC_SCENE 0x43535450u /* "PTSC" */
#define PT_VERSION 1u

/* Header words. */
#define PT_H_MAGIC 0
#define PT_H_VERSION 1
#define PT_H_WIDTH 2
#define PT_H_HEIGHT 3
#define PT_H_SPP 4 /* samples per pixel per pass */
#define PT_H_PASSES 5
#define PT_H_DEPTH 6
#define PT_H_SEED 7
#define PT_H_NMAT 8
#define PT_H_NQUAD 9
#define PT_H_NSPH 10
#define PT_H_NTRI 11
#define PT_H_LIGHT 12    /* the quad next-event estimation samples (PT_NO_LIGHT: none) */
#define PT_H_EXPOSURE 13
#define PT_H_CLAMP 14    /* the per-sample radiance clamp */
#define PT_H_WORDS 15    /* the scene's length in words */
#define PT_H_EYE 16      /* the pinhole: eye, the image's top-left corner, horizontal and vertical spans */
#define PT_H_CORNER 19
#define PT_H_HORIZ 22
#define PT_H_VERT 25
#define PT_H_MESH 28     /* the triangles' bounding sphere: center, radius */
#define PT_HEADER_WORDS 32
#define PT_MAT_WORDS 8   /* kind, albedo rgb, emission rgb, param (metal: fuzz; glass: ior) */
#define PT_QUAD_WORDS 16 /* p, u, v, n (unit), 1/u.u, 1/v.v, area, material */
#define PT_SPH_WORDS 8   /* center, radius, 1/radius, material, 0, 0 */
#define PT_TRI_WORDS 16  /* v0, e1, e2, n (unit), material, 0, 0, 0 */
#define PT_SCENE_MAX_WORDS 1024
#define PT_NO_LIGHT 0xffffffffu
#define PT_DIFFUSE 0u
#define PT_METAL 1u
#define PT_GLASS 2u
#define PT_EMIT 3u

#define PT_TILE_W 32u
#define PT_TILE_H 24u
#define PT_TILE_PIX (PT_TILE_W * PT_TILE_H)
#define PT_MAX_TILES 300u        /* tiles the view holds: 640 x 360 */
#define PT_FLEET_MAX_TILES 4096u /* tiles of an image rendered as pass sums only: 2048 x 1536 */
#define PT_TONEMAP_ACES 0u
#define PT_TONEMAP_REINHARD 1u
#define PT_TONEMAPS 2u

/* Where each part of a scene starts, from its header. */
typedef struct {
    uint32_t nquad, nsph, ntri;
    size_t mat, quad, sph, tri;
    uint32_t light;
} pt_layout;

pt_layout pt_layout_of(const uint32_t *sc);

/* 0 when the scene's words are well formed (every index in range, the image a whole number of tiles
 * within PT_MAX_TILES); else which rule failed, 1..10. len is the words available. */
uint32_t pt_validate(const uint32_t *sc, size_t len);
/* pt_validate with at most max_tiles tiles (PT_FLEET_MAX_TILES: pass sums only). */
uint32_t pt_validate_in(const uint32_t *sc, size_t len, uint32_t max_tiles);

/* The scene words a job's setup may set: the image size, samples per pass, passes, depth, seed. */
#define PT_SETTABLE_COUNT 6
extern const size_t pt_settable[PT_SETTABLE_COUNT];
/* OP_SET's arg: word := value (< 2^24). */
static inline uint32_t pt_set_arg(size_t word, uint32_t value) { return (uint32_t)word << 24 | (value & 0x00ffffffu); }
/* Applies an OP_SET arg to a scene of len words; false (nothing changed) for a word not settable. */
bool pt_set_param(uint32_t *sc, size_t len, uint32_t arg);

static inline uint32_t pt_tiles_x(const uint32_t *sc) { return sc[PT_H_WIDTH] / PT_TILE_W; }
static inline uint32_t pt_tiles(const uint32_t *sc) {
    return (sc[PT_H_WIDTH] / PT_TILE_W) * (sc[PT_H_HEIGHT] / PT_TILE_H);
}
/* Tile t's top-left pixel. */
static inline void pt_tile_origin(const uint32_t *sc, uint32_t t, uint32_t *x, uint32_t *y) {
    uint32_t tx = pt_tiles_x(sc);
    *x = (t % tx) * PT_TILE_W;
    *y = (t / tx) * PT_TILE_H;
}

#endif
