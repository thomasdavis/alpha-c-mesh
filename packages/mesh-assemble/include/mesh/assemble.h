/* @ajax/mesh-assemble: the assembler of a fleet-rendered pathtrace image (assemble.rs), sans IO:
 * blobd feeds it the job's committed results (each item's blob reference and pass-sum digest) and the
 * blobs' bytes, and asks it what to fetch next and for the image.
 *
 * An item w = pass x tiles + tile is one tile's pass sums, computed from +0 wherever it ran. The image
 * is the reference's because each tile's pass sums are folded in pass order -- acc = ((0 + P0) + P1)
 * + .., every add in the cluster float mode Z -- and exactly once per (tile, pass): a tile advances
 * only by its next pass, so a blob that arrives early waits (the caller fetches at most WINDOW passes
 * ahead) and a second copy of a folded pass is dropped. The 8-bit pixels are the tone map of the
 * accumulators so far, recomputed when a tile changed or the tone map did. Every payload is checked
 * against its item's pass-sum digest (the job record's J_PART) before it is folded. */
#ifndef MESH_ASSEMBLE_H
#define MESH_ASSEMBLE_H

#include "mesh/jobs/id.h"
#include "mesh/jobs/spec.h"
#include "mesh/omap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Passes ahead of a tile's next one that are fetched and kept waiting. */
#define MESH_ASSEMBLE_WINDOW 2u

/* A committed item as the assembler needs it. */
typedef struct {
    mj_hash hash;
    uint32_t len, holders;
    uint32_t member; /* whose result was committed */
    uint64_t part;   /* the item's pass-sum digest (its job record's J_PART) */
} masm_item;

/* The pass-sum digest in a pathtrace job record (job_out: J_PART, a u64 at word 2); false if short. */
bool masm_part_of(const uint8_t *out, size_t n, uint64_t *part);
/* The scene a job renders: the default scene with its setup applied in order (5:0, 7:<word << 24 |
 * value>), valid for pass sums. Writes PT_DEFAULT_SCENE_WORDS words; false with *err. */
bool masm_job_scene(const mj_setup *setup, size_t n, uint32_t *sc, char **err);
/* Whether a job is a pathtrace image (its program renders one, its op leaves payloads). */
bool masm_renders(const char *program, uint32_t op);

typedef struct masm masm;

masm *masm_new(mj_job_id job, const mj_setup *setup, size_t nsetup, uint32_t tone, char **err);
void masm_free(masm *a);

mj_job_id masm_job(const masm *a);
const uint32_t *masm_scene(const masm *a);
uint32_t masm_tiles(const masm *a);
uint32_t masm_passes(const masm *a);
uint32_t masm_tone(const masm *a);
uint64_t masm_items(const masm *a);
uint32_t masm_width(const masm *a);
uint32_t masm_height(const masm *a);
uint32_t masm_spp(const masm *a);
bool masm_complete(const masm *a);
uint32_t masm_tiles_done(const masm *a);
uint64_t masm_folded(const masm *a);
uint64_t masm_committed(const masm *a);
/* Items folded per member (member -> uint64_t). */
const momap *masm_by_member(const masm *a);
/* Payloads refused (length or digest wrong): their count, and the i-th (item, why). */
size_t masm_bad_count(const masm *a);
const char *masm_bad_at(const masm *a, size_t i, uint64_t *item);
/* Tile t: passes folded, and the member of the last one (UINT32_MAX: none). */
void masm_tile(const masm *a, uint32_t t, uint32_t *passes, uint32_t *owner);

/* A committed result (each at most once: a repeat, or an item folded already, is ignored). */
void masm_commit(masm *a, uint64_t item, masm_item it);
/* The committed items to fetch next, the lowest passes first: each tile's next WINDOW passes that are
 * committed, not here yet and not asked for (asked(ctx, item)). At most budget; out[i] items. */
size_t masm_wants(const masm *a, bool (*asked)(void *ctx, uint64_t item), void *ctx, size_t budget, uint64_t *items, masm_item *its);
/* The committed item for item if it is still wanted, or NULL. */
const masm_item *masm_wanted(const masm *a, uint64_t item);
/* A blob's bytes for item (its sha256 checked by the caller): checked against the item's pass-sum
 * digest, then folded with every pass of its tile that is ready, in order. Returns the passes folded
 * now, or -1 when refused (kept as bad; *err says why: free it). */
long masm_blob(masm *a, uint64_t item, const uint8_t *bytes, size_t n, char **err);
/* The tone map the pixels are shown with; every tile is re-toned (no accumulator changes). */
void masm_set_tone(masm *a, uint32_t tone);
/* The 8-bit pixels (0xffBBGGRR), tile-major, re-toned where a tile changed. */
const uint32_t *masm_rgb(masm *a);
/* A tile's accumulators (3 x TILE_PIX f32 bits), or NULL before its first pass. */
const uint32_t *masm_acc(const masm *a, uint32_t t);
/* The image's digests as kcheck prints them: FNV over every tile's accumulator words in tile order
 * (zeros for a tile with no pass) and over its pixels. */
void masm_digests(masm *a, uint64_t *acc, uint64_t *pixels);
/* The 8-bit RGB raster (row-major), PPM, BMP, and the PPM's sha256 in hex: new buffers. */
uint8_t *masm_raster(masm *a, size_t *len);
uint8_t *masm_ppm(masm *a, size_t *len);
uint8_t *masm_bmp_of(masm *a, size_t *len);
char *masm_sha256(masm *a);

#endif
