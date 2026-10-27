/* @ajax/mesh-abi-pipe-ring: A pipe inbox's two lock-free MPMC rings: laps, record sums, reserve and commit.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PIPE_RING_H
#define MESH_ABI_PIPE_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/words.h"
#include "mesh/abi/pipe_hash.h"

/* The ring's words, relative to the inbox start. */
#define MESH_PIPE_PI_HEAD 0x00
#define MESH_PIPE_PI_FULL 0x04
#define MESH_PIPE_PI_TAIL 0x40
/* Pops that found the ring empty (consumers count them; informational). */
#define MESH_PIPE_PI_EMPTY 0x44
#define MESH_PIPE_PI_LAPS 0x80
/* The largest record (bytes) and inbox (records, both sides). */
#define MESH_PIPE_REC_MAX 4096u
#define MESH_PIPE_CAP_MAX ((uint32_t)(1 << 16))
/* An inbox's producer sides (`Inbox::side`). */
#define MESH_PIPE_SIDE_CPU 0u
#define MESH_PIPE_SIDE_GPU 1u
/* The bound of every ring loop (a CAS lost to another producer or consumer is one try). */
#define MESH_PIPE_TRIES 256u
/* What a reserved, not yet committed cell's sum word holds: its producer's slot. */
#define MESH_PIPE_OWNER_TAG 0x7e57a000u

/* What a reserved, not yet committed cell's sum word holds: its producer's slot. */
static inline uint32_t mesh_pipe_owner_tag(uint32_t slot) { return MESH_PIPE_OWNER_TAG | (slot & 0xfffu); }

/* Where the record sums start, and the records. */
static inline size_t mesh_pipe_inbox_sums(uint32_t cap) { return MESH_PIPE_PI_LAPS + 4 * (size_t)cap; }
static inline size_t mesh_pipe_inbox_data(uint32_t cap) { return (MESH_PIPE_PI_LAPS + 8 * (size_t)cap + 15) & ~(size_t)15; }
/* One ring's bytes (CAP records), 64-aligned. */
static inline size_t mesh_pipe_ring_bytes(uint32_t cap, uint32_t rec) {
    return (mesh_pipe_inbox_data(cap) + (size_t)cap * rec + 63) & ~(size_t)63;
}
/* The inbox's bytes in the view: two rings of cap / 2. */
static inline size_t mesh_pipe_inbox_bytes(uint32_t cap, uint32_t rec) { return 2 * mesh_pipe_ring_bytes(cap / 2, rec); }

/* One ring: where it is and its shape. */
typedef struct {
    size_t base; /* the ring's first byte (absolute, as mesh_words takes it) */
    uint32_t cap;
    uint32_t rec;
} mesh_pipe_ring;

/* One inbox: two rings of cap / 2, one per producer side. */
typedef struct {
    size_t base;  /* the inbox's first byte (absolute, as mesh_words takes it) */
    uint32_t cap; /* records, both sides (the manifest's CAP) */
    uint32_t rec;
} mesh_pipe_inbox;

static inline bool mesh_pipe_is_pow2(uint32_t x) { return x != 0 && (x & (x - 1)) == 0; }
/* trailing_zeros, 32 for 0. */
static inline uint32_t mesh_pipe_ctz(uint32_t x) {
    uint32_t n = 0;
    if (x == 0) return 32;
    while ((x & 1u) == 0) x >>= 1, n++;
    return n;
}
static inline uint32_t mesh_pipe_ring_shift(const mesh_pipe_ring *r) { return mesh_pipe_ctz(r->cap); }
/* 2^33 / cap - 1: lap words live modulo 2^33 / cap. */
static inline uint32_t mesh_pipe_ring_lap_mask(const mesh_pipe_ring *r) {
    return (uint32_t)((UINT64_C(1) << (33 - mesh_pipe_ring_shift(r))) - 1);
}
static inline uint32_t mesh_pipe_ring_empty(const mesh_pipe_ring *r, uint32_t p) {
    return (2 * (p >> mesh_pipe_ring_shift(r))) & mesh_pipe_ring_lap_mask(r);
}
static inline uint32_t mesh_pipe_ring_full(const mesh_pipe_ring *r, uint32_t p) {
    return (2 * (p >> mesh_pipe_ring_shift(r)) + 1) & mesh_pipe_ring_lap_mask(r);
}
/* The lap word of position p. */
static inline size_t mesh_pipe_ring_lap(const mesh_pipe_ring *r, uint32_t p) {
    return r->base + MESH_PIPE_PI_LAPS + 4 * (size_t)(p & (r->cap - 1));
}
/* The sum word of position p's record. */
static inline size_t mesh_pipe_ring_sum(const mesh_pipe_ring *r, uint32_t p) {
    return r->base + mesh_pipe_inbox_sums(r->cap) + 4 * (size_t)(p & (r->cap - 1));
}
/* The record of position p. */
static inline size_t mesh_pipe_ring_cell(const mesh_pipe_ring *r, uint32_t p) {
    return r->base + mesh_pipe_inbox_data(r->cap) + (size_t)(p & (r->cap - 1)) * r->rec;
}
/* A shape the runtimes accept (meshd checks manifests the same way). */
static inline bool mesh_pipe_ring_valid(const mesh_pipe_ring *r) {
    return mesh_pipe_is_pow2(r->cap) && r->cap >= 2 && r->cap <= MESH_PIPE_CAP_MAX && r->rec >= 4 &&
           r->rec <= MESH_PIPE_REC_MAX && r->rec % 4 == 0;
}

/* Claims the next position to fill for producer slot `me` (its owner_tag goes
 * into the cell's sum word): true with *p. False: full (PI_FULL counted), or a
 * cell no producer may take now (TRIES spent). */
static inline bool mesh_pipe_ring_reserve_push(const mesh_pipe_ring *r, const mesh_words *w, uint32_t me, uint32_t *p) {
    for (uint32_t i = 0; i < MESH_PIPE_TRIES; i++) {
        uint32_t q = mesh_words_load(w, r->base + MESH_PIPE_PI_HEAD);
        uint32_t l = mesh_words_load(w, mesh_pipe_ring_lap(r, q));
        if (l == mesh_pipe_ring_empty(r, q)) {
            if (mesh_words_cas(w, r->base + MESH_PIPE_PI_HEAD, q, q + 1) == q) {
                mesh_words_store(w, mesh_pipe_ring_sum(r, q), mesh_pipe_owner_tag(me));
                *p = q;
                return true;
            }
        } else if (mesh_words_load(w, r->base + MESH_PIPE_PI_HEAD) == q) {
            /* The cell still holds position p - cap (full: backpressure), or a
             * word that is neither: a cell its producer never committed, or a
             * ring being reset.  Busy either way. */
            if (l == ((mesh_pipe_ring_empty(r, q) - 1) & mesh_pipe_ring_lap_mask(r)))
                mesh_words_add(w, r->base + MESH_PIPE_PI_FULL, 1);
            return false;
        }
    }
    return false;
}
/* Publishes position p's record (written at cell(p), its sum at sum(p)). */
static inline void mesh_pipe_ring_commit_push(const mesh_pipe_ring *r, const mesh_words *w, uint32_t p) {
    mesh_words_store(w, mesh_pipe_ring_lap(r, p), mesh_pipe_ring_full(r, p));
}

/* Claims the next position to drain: true with *t; false: empty. The caller
 * checks the record's sum itself (kcheck's sink); runtimes use take. */
static inline bool mesh_pipe_ring_reserve_pop(const mesh_pipe_ring *r, const mesh_words *w, uint32_t *t) {
    for (uint32_t i = 0; i < MESH_PIPE_TRIES; i++) {
        uint32_t q = mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL);
        uint32_t l = mesh_words_load(w, mesh_pipe_ring_lap(r, q));
        if (l == mesh_pipe_ring_full(r, q)) {
            if (mesh_words_cas(w, r->base + MESH_PIPE_PI_TAIL, q, q + 1) == q) {
                *t = q;
                return true;
            }
        } else if (mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL) == q) {
            /* Not yet written (empty, or a producer between its reserve and commit). */
            return false;
        }
    }
    return false;
}
/* Reads the next record into out (len bytes, the ring's rec) and claims it:
 * its sum is checked before the claim, so a record whose words are not visible
 * here yet (a producer on a device that is coherent only at its own
 * boundaries) stays in the ring, as if not yet pushed. False: empty (or not
 * yet visible). The cell is the caller's until commit_pop. */
static inline bool mesh_pipe_ring_take(const mesh_pipe_ring *r, const mesh_words *w, uint8_t *out, size_t len, uint32_t *t) {
    for (uint32_t i = 0; i < MESH_PIPE_TRIES; i++) {
        uint32_t q = mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL);
        uint32_t l = mesh_words_load(w, mesh_pipe_ring_lap(r, q));
        if (l == mesh_pipe_ring_full(r, q)) {
            mesh_words_read(w, mesh_pipe_ring_cell(r, q), out, len);
            if (mesh_pipe_rec_sum_at(out, len, q) != mesh_words_load(w, mesh_pipe_ring_sum(r, q))) return false;
            /* A position's record cannot change before its consumer's commit_pop: what was read is it. */
            if (mesh_words_cas(w, r->base + MESH_PIPE_PI_TAIL, q, q + 1) == q) {
                *t = q;
                return true;
            }
        } else if (mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL) == q) {
            return false;
        }
    }
    return false;
}
/* Frees position t's cell for the producers (its record was read). */
static inline void mesh_pipe_ring_commit_pop(const mesh_pipe_ring *r, const mesh_words *w, uint32_t t) {
    mesh_words_store(w, mesh_pipe_ring_lap(r, t), mesh_pipe_ring_empty(r, t + r->cap));
}

/* Records waiting (reserved or committed): head - tail, at most cap. */
static inline uint32_t mesh_pipe_ring_len(const mesh_pipe_ring *r, const mesh_words *w) {
    uint32_t n = mesh_words_load(w, r->base + MESH_PIPE_PI_HEAD) - mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL);
    return n < r->cap ? n : r->cap;
}
/* Free cells now (a hint: other producers may take them). */
static inline uint32_t mesh_pipe_ring_free(const mesh_pipe_ring *r, const mesh_words *w) {
    return r->cap - mesh_pipe_ring_len(r, w);
}

/* Calls f(ctx, p) for every position in [tail, head) that producer slot s
 * reserved and has not committed (its owner tag in the sum word). Only
 * meaningful once s runs no code: then none of them will ever commit. */
static inline void mesh_pipe_ring_abandoned(const mesh_pipe_ring *r, const mesh_words *w, uint32_t s,
                                            void (*f)(void *ctx, uint32_t p), void *ctx) {
    uint32_t head = mesh_words_load(w, r->base + MESH_PIPE_PI_HEAD);
    uint32_t tail = mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL);
    uint32_t n = head - tail < r->cap ? head - tail : r->cap;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = tail + i;
        if (mesh_words_load(w, mesh_pipe_ring_lap(r, p)) == mesh_pipe_ring_empty(r, p) &&
            mesh_words_load(w, mesh_pipe_ring_sum(r, p)) == mesh_pipe_owner_tag(s))
            f(ctx, p);
    }
}
/* Releases dead producer s's position p (see abandoned) once the consumers
 * reached it: 1 skipped, 0 not yet at the front (call again later), -1 not
 * that dead cell any more (committed after all, or the ring was reset).
 * Consumers never claim a cell whose lap word is not full, so the CAS on
 * PI_TAIL races no one. */
static inline int mesh_pipe_ring_skip_dead(const mesh_pipe_ring *r, const mesh_words *w, uint32_t s, uint32_t p) {
    if (mesh_words_load(w, mesh_pipe_ring_lap(r, p)) != mesh_pipe_ring_empty(r, p) ||
        mesh_words_load(w, mesh_pipe_ring_sum(r, p)) != mesh_pipe_owner_tag(s))
        return -1;
    uint32_t t = mesh_words_load(w, r->base + MESH_PIPE_PI_TAIL);
    /* Behind the tail already (a reset restarted the ring's positions): not this cell. */
    if (p - t >= r->cap) return -1;
    if (t != p) return 0;
    if (mesh_words_cas(w, r->base + MESH_PIPE_PI_TAIL, p, p + 1) != p) return 0;
    /* The tag goes, so a later lap's reserve of this cell is never mistaken for it. */
    mesh_words_store(w, mesh_pipe_ring_sum(r, p), 0);
    mesh_pipe_ring_commit_pop(r, w, p);
    return 1;
}

/* Side k's ring (SIDE_CPU, SIDE_GPU). */
static inline mesh_pipe_ring mesh_pipe_inbox_side(const mesh_pipe_inbox *in, uint32_t k) {
    mesh_pipe_ring r = {in->base + (size_t)(k & 1u) * mesh_pipe_ring_bytes(in->cap / 2, in->rec), in->cap / 2, in->rec};
    return r;
}
/* A shape the runtimes accept (meshd checks manifests with it). */
static inline bool mesh_pipe_inbox_valid(const mesh_pipe_inbox *in) {
    mesh_pipe_ring r = mesh_pipe_inbox_side(in, 0);
    return in->cap >= 4 && mesh_pipe_ring_valid(&r) && mesh_pipe_is_pow2(in->cap);
}
/* The side a consumer drains first: the fuller one (CPU on a tie). */
static inline uint32_t mesh_pipe_inbox_first(const mesh_pipe_inbox *in, const mesh_words *w) {
    mesh_pipe_ring g = mesh_pipe_inbox_side(in, MESH_PIPE_SIDE_GPU), c = mesh_pipe_inbox_side(in, MESH_PIPE_SIDE_CPU);
    return mesh_pipe_ring_len(&g, w) > mesh_pipe_ring_len(&c, w) ? 1u : 0u;
}
/* ring_take on the fuller side, then the other: true with the ring and position taken. */
static inline bool mesh_pipe_inbox_take(const mesh_pipe_inbox *in, const mesh_words *w, uint8_t *out, size_t len,
                                        mesh_pipe_ring *ring, uint32_t *t) {
    uint32_t k = mesh_pipe_inbox_first(in, w);
    uint32_t sides[2] = {k, k ^ 1u};
    for (int i = 0; i < 2; i++) {
        mesh_pipe_ring r = mesh_pipe_inbox_side(in, sides[i]);
        if (mesh_pipe_ring_take(&r, w, out, len, t)) {
            *ring = r;
            return true;
        }
    }
    return false;
}
/* Records waiting, both sides. */
static inline uint32_t mesh_pipe_inbox_len(const mesh_pipe_inbox *in, const mesh_words *w) {
    mesh_pipe_ring a = mesh_pipe_inbox_side(in, 0), b = mesh_pipe_inbox_side(in, 1);
    return mesh_pipe_ring_len(&a, w) + mesh_pipe_ring_len(&b, w);
}

#endif
