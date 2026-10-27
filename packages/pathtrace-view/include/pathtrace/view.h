/* @ajax/pathtrace-view: the view of programs/pathtrace (and its variant pathtrace-live): what the
 * program, kcheck, mesh-top and the http program agree on (pathtrace-core lib.rs `view`). Byte
 * offsets into the view. Tile-major: tile t's pixels are contiguous (row-major within it), so a
 * tile's result is one block (a fleet job's payload). Freestanding. */
#ifndef PATHTRACE_VIEW_H
#define PATHTRACE_VIEW_H

#include "pathtrace/scene.h"

#include <stdint.h>

#define PT_V_MAGIC 0x000u  /* u32 "PTVW" once OP_INIT ran */
#define PT_V_NEXT 0x004u   /* pathtrace-live's next progressive work item: w = pass x tiles + tile */
#define PT_V_TONEMAP 0x008u
#define PT_V_GEN 0x00cu    /* scene generation: +1 on every scene load or restart */
#define PT_V_TILES 0x010u
#define PT_V_DONE 0x014u   /* tiles with every pass done */
#define PT_V_WIDTH 0x018u
#define PT_V_HEIGHT 0x01cu
#define PT_V_SPP 0x020u
#define PT_V_PASSES 0x024u
#define PT_V_LAST_TILE 0x028u /* the last tile rendered, by whom (system; PEER_NONE single), where */
#define PT_V_LAST_SYSTEM 0x02cu
#define PT_V_LAST_EXEC 0x030u
#define PT_V_SCENE_ERR 0x034u /* the last scene request's error code (0 ok) */
#define PT_V_ITEMS 0x038u     /* work items (passes or job tiles) run */
#define PT_VIEW_MAGIC 0x57565450u /* "PTVW" */
#define PT_V_JOB_BASE 0x100u /* u64: the job item base meshd writes (manifest job_base) */
#define PT_V_JOB_OUT 0x200u  /* job records: PT_JOB_REC bytes at V_JOB_OUT + JOB_REC (ticket % 64) */
#define PT_JOB_REC 32u
/* Words of a job record: the item's tile and pass, the digest of its pass sums (u64), the tile's
 * accumulator digest after the fold (u64), its microseconds, its executor. */
#define PT_J_TILE 0u
#define PT_J_PASS 1u
#define PT_J_PART 2u /* u64 lo, hi */
#define PT_J_ACC 4u  /* u64 lo, hi */
#define PT_J_US 6u
#define PT_J_EXEC 7u
/* The request window: a scene's words (raw, little-endian), or text ("restart", "status"). */
#define PT_V_REQ 0x0a00u
#define PT_REQ_MAX 0x1000u
#define PT_V_RESP 0x1a00u
#define PT_RESP_MAX 0x100u
#define PT_V_SCENE 0x2000u
/* Per tile, PT_META bytes: passes done (PT_FOLDING set while a pass is being added), system,
 * exec | node << 8, last item's us, acc digest (u64), rgb digest (u64). */
#define PT_V_META 0x3000u
#define PT_META 32u
#define PT_M_PASSES 0u
#define PT_M_SYSTEM 4u
#define PT_M_EXEC 8u
#define PT_M_US 12u
#define PT_M_ACC 16u
#define PT_M_RGB 24u
#define PT_FOLDING 0x80000000u /* M_PASSES while pass p is folded in: p | FOLDING */
#define PT_VIEW_SNAP_BYTES 0x5600u /* the prefix the snapshot copies: header, scene, tiles' meta */
/* The payload ring (manifest job_payload): an OP_JOB / OP_PASS item writes its pass sums (f32 r g b
 * per pixel, row-major) at V_PAYLOAD + PAYLOAD (ticket % 64) before it returns. */
#define PT_V_PAYLOAD 0x6000u
#define PT_PAYLOAD (12u * PT_TILE_PIX)
/* A view this long (and no longer) holds no tile: a fleet job's CPU variant (pathtrace-cpu). */
#define PT_FLEET_VIEW_BYTES (PT_V_PAYLOAD + 64u * PT_PAYLOAD)
#define PT_V_RGB PT_FLEET_VIEW_BYTES                          /* 0xffBBGGRR per pixel, tile-major */
#define PT_V_ACC (PT_V_RGB + 4u * PT_TILE_PIX * PT_MAX_TILES) /* f32 r g b per pixel: the sums */
#define PT_VIEW_BYTES (PT_V_ACC + 12u * PT_TILE_PIX * PT_MAX_TILES)
/* Executors (M_EXEC's low byte). */
#define PT_EXEC_CUDA 1u
#define PT_EXEC_METAL 2u
#define PT_EXEC_WASM 3u
#define PT_EXEC_CPU 4u
/* Ops (task pathtrace <op> <arg>). */
#define PT_OP_RESTART 3u /* forget every tile (the scene stays) */
#define PT_OP_TONEMAP 4u /* tone map arg (TONEMAP_*) and re-tone every tile, no render */
#define PT_OP_DEFAULT 5u /* load the default scene (and restart) */
#define PT_OP_PASS 6u    /* a job item as pass sums only: payload and record, no fold, no wait */
#define PT_OP_SET 7u     /* a job's setup: scene word arg >> 24 := arg & 0xffffff, no restart */

static inline uint32_t pt_view_rgb(uint32_t t) { return PT_V_RGB + 4u * PT_TILE_PIX * t; }
static inline uint32_t pt_view_acc(uint32_t t) { return PT_V_ACC + 12u * PT_TILE_PIX * t; }
static inline uint32_t pt_view_meta(uint32_t t) { return PT_V_META + PT_META * t; }
static inline uint32_t pt_view_payload(uint32_t slot) { return PT_V_PAYLOAD + PT_PAYLOAD * slot; }

_Static_assert(PT_V_SCENE + 4u * PT_SCENE_MAX_WORDS <= PT_V_META &&
                   PT_V_META + PT_META * PT_MAX_TILES <= PT_VIEW_SNAP_BYTES && PT_VIEW_SNAP_BYTES <= PT_V_RGB,
               "pathtrace view regions overlap");
_Static_assert(PT_V_JOB_OUT + 64u * PT_JOB_REC <= PT_V_REQ && PT_V_REQ + PT_REQ_MAX <= PT_V_RESP &&
                   PT_V_RESP + PT_RESP_MAX <= PT_V_SCENE,
               "pathtrace view windows overlap");
_Static_assert(PT_VIEW_BYTES <= 7u << 20, "pathtrace view exceeds 7 MiB");

#endif
