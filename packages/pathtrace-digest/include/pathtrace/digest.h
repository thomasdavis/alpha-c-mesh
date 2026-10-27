/* @ajax/pathtrace-digest: the digests of programs/pathtrace (pathtrace-core lib.rs): FNV-1a 64 over
 * u32 words, a tile's digest folded from its 32 column digests, and a pass-sum payload's digest (a
 * fleet job item's J_PART, as meshd's assembler checks it). Freestanding. */
#ifndef PATHTRACE_DIGEST_H
#define PATHTRACE_DIGEST_H

#include <stddef.h>
#include <stdint.h>

#define PT_FNV_OFFSET 0xcbf29ce484222325ull
#define PT_FNV_PRIME 0x00000100000001b3ull

static inline uint64_t pt_fnv(uint64_t h, uint32_t w) { return (h ^ w) * PT_FNV_PRIME; }

/* FNV-1a 64 over the 32 column digests (lo word, hi word) in column order. */
uint64_t pt_fold(const uint64_t cols[32]);
/* FNV-1a 64 over n words from PT_FNV_OFFSET. */
uint64_t pt_fnv_words(const uint32_t *w, size_t n);
/* A payload's digest: words are f32 bits, row-major, r g b per pixel (TILE_PIX x 3); per column FNV
 * over its rows' r g b, the columns folded in order. */
uint64_t pt_payload_digest(const uint32_t *words);

#endif
