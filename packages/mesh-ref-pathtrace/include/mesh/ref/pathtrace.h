/* @ajax/mesh-ref-pathtrace: programs/pathtrace's CPU reference (mesh-ref pathtrace.rs): the renderer
 * in Z (@ajax/pathtrace-z), the same code the wasm image runs, so a tile rendered here is bit for bit
 * the tile every backend must render. Also the image's assembly from tiles (tile-major -> raster), its
 * PPM, and the digests kcheck and the demo compare. Host (threads, malloc). */
#ifndef MESH_REF_PATHTRACE_H
#define MESH_REF_PATHTRACE_H

#include "pathtrace/default_scene.h" /* DEFAULT_SCENE, as mesh-ref re-exports it */
#include "pathtrace/scene.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One tile as the view holds it: the accumulated sums (f32 bits, r g b a pixel, row-major), the 8-bit
 * pixels (0xffBBGGRR), their digests, and the digest of each pass's sums. */
typedef struct {
    uint32_t t, passes;
    uint32_t acc[3 * PT_TILE_PIX];
    uint32_t rgb[PT_TILE_PIX];
    uint64_t acc_digest, rgb_digest;
    uint64_t *part_digests; /* passes of them; free with mesh_pt_tile_free */
} mesh_pt_tile;

/* Tile t after passes 0..passes (each pass's sums from +0, folded in pass order), tone mapped with op.
 * False only when memory runs out. */
bool mesh_pt_tile_render(const uint32_t *sc, uint32_t t, uint32_t passes, uint32_t op, mesh_pt_tile *out);
void mesh_pt_tile_free(mesh_pt_tile *tile);
/* Tiles ts[0..n) on every core into out[0..n) (each tile independent: order and threads change
 * nothing). False when memory runs out (out is then freed). */
bool mesh_pt_tiles(const uint32_t *sc, const uint32_t *ts, size_t n, uint32_t passes, uint32_t op, mesh_pt_tile *out);

/* The image's 8-bit RGB raster (row-major, 3 bytes a pixel, width x height x 3 bytes into out) from
 * tile-major 0xffBBGGRR words (tiles x PT_TILE_PIX). */
void mesh_pt_raster(const uint32_t *sc, const uint32_t *rgb, uint8_t *out);
/* A binary PPM (P6) of a raster, malloc'd; its length in *len. NULL when memory runs out. */
uint8_t *mesh_pt_ppm(uint32_t w, uint32_t h, const uint8_t *raster, size_t *len);

#endif
