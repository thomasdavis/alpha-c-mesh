/* @ajax/mesh-abi-pipe: Pipes between programs: push into a connected inbox, pop your own.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PIPE_H
#define MESH_ABI_PIPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"
#include "mesh/abi/header.h"
#include "mesh/abi/program_table.h"
#include "mesh/abi/pipe_table.h"
#include "mesh/abi/pipe_ring.h"
#include "mesh/abi/words.h"
#include "mesh/abi/pipe_hash.h"

/* The runtimes' return codes (wasm `mesh::pipe_*`, mesh-worker, mesh-rt):
 * 1 done, 0 full (push) / empty (pop), negative: a PipeErr. */
#define MESH_PIPE_OK 1
#define MESH_PIPE_NONE 0
#define MESH_PIPE_NOT_CONNECTED -1
#define MESH_PIPE_SIZE -2
#define MESH_PIPE_GONE -3

/* Why a push or pop could not run (FAULT_PIPE for the first two). Functions
 * below return 1 for Ok(true), 0 for Ok(false), and -e for Err(e), so that
 * -NOT_CONNECTED, -SIZE and -GONE equal the runtimes' PIPE_* codes. */
typedef enum {
    /* This slot does not feed that one (no connected `feeds` entry), or it has no inbox. */
    MESH_PIPE_ERR_NOT_CONNECTED = 1,
    /* The record's size is not the inbox's PP_IN_REC. */
    MESH_PIPE_ERR_SIZE = 2,
    /* The consumer is not loaded (or FAILED: a stalled ring is not written to). */
    MESH_PIPE_ERR_GONE = 3,
    /* meshd holds the inbox (it is being reset): try later, like a full ring. */
    MESH_PIPE_ERR_BUSY = 4,
} mesh_pipe_err;

/* The runtimes' return code of a result: PIPE_OK, PIPE_NONE (full, empty or
 * busy), or a negative PIPE_* code. */
static inline int32_t mesh_pipe_code(int r) {
    if (r > 0) return MESH_PIPE_OK;
    if (r == 0 || r == -MESH_PIPE_ERR_BUSY) return MESH_PIPE_NONE;
    if (r == -MESH_PIPE_ERR_NOT_CONNECTED) return MESH_PIPE_NOT_CONNECTED;
    if (r == -MESH_PIPE_ERR_SIZE) return MESH_PIPE_SIZE;
    return MESH_PIPE_GONE;
}

/* Slot s's pipe block and view offset, read under its PT_GEN seqlock into
 * out: (PT_STATE, PT_VIEW_OFF, PP_IN_OFF, PP_IN_CAP, PP_IN_REC,
 * PP_FEEDS_MASK, PP_IN_HOLD). False: it kept changing (TRIES reads; the
 * caller treats it as busy). */
static inline bool mesh_pipe_read_block(const mesh_words *w, uint32_t s, uint32_t out[7]) {
    size_t e = mesh_prog_entry(s), p = mesh_pipe_entry(s);
    for (uint32_t i = 0; i < MESH_PIPE_TRIES; i++) {
        uint32_t g = mesh_words_load(w, e + MESH_PT_GEN);
        if (g & 1u) continue;
        out[0] = mesh_words_load(w, e + MESH_PT_STATE);
        out[1] = mesh_words_load(w, e + MESH_PT_VIEW_OFF);
        out[2] = mesh_words_load(w, p + MESH_PP_IN_OFF);
        out[3] = mesh_words_load(w, p + MESH_PP_IN_CAP);
        out[4] = mesh_words_load(w, p + MESH_PP_IN_REC);
        out[5] = mesh_words_load(w, p + MESH_PP_FEEDS_MASK);
        out[6] = mesh_words_load(w, p + MESH_PP_IN_HOLD);
        if (mesh_words_load(w, e + MESH_PT_GEN) == g) return true;
    }
    return false;
}

/* Slot c's inbox as slot `me` may push into it (`me == c`: its own, to pop):
 * 0 with *out, or -e. An empty slot c is Gone before anything else: a
 * producer that found its consumer just before it was unloaded (its feeds bit
 * cleared right after) is told so, not faulted. */
static inline int mesh_pipe_target(const mesh_words *w, uint32_t me, uint32_t c, mesh_pipe_inbox *out) {
    uint32_t b[7], mine[7];
    if (me >= MESH_PROG_SLOTS || c >= MESH_PROG_SLOTS) return -MESH_PIPE_ERR_NOT_CONNECTED;
    if (!mesh_pipe_read_block(w, c, b)) return -MESH_PIPE_ERR_BUSY;
    uint32_t st = b[0], view = b[1], off = b[2], cap = b[3], rec = b[4], hold = b[6];
    if (st == MESH_ST_EMPTY) return -MESH_PIPE_ERR_GONE;
    if (me != c) {
        if (!mesh_pipe_read_block(w, me, mine)) return -MESH_PIPE_ERR_BUSY;
        if ((mine[5] & (1u << c)) == 0) return -MESH_PIPE_ERR_NOT_CONNECTED;
    }
    if (me != c && st == MESH_ST_FAILED) return -MESH_PIPE_ERR_GONE;
    if (cap == 0) return -MESH_PIPE_ERR_NOT_CONNECTED;
    if (me != c && hold != 0) return -MESH_PIPE_ERR_BUSY;
    out->base = (size_t)view + (size_t)off;
    out->cap = cap;
    out->rec = rec;
    return 0;
}

/* Producer slot me's pushes in flight: +1 in its word of the current epoch's
 * parity (returned, for flight_end) before it resolves the consumer's block;
 * the fence orders that before the block's loads (meshd stores PP_IN_HOLD,
 * bumps H_PIPE_EPOCH, fences, then reads the old parity's words). */
static inline uint32_t mesh_pipe_flight_begin(const mesh_words *w, uint32_t me) {
    uint32_t parity = mesh_words_load(w, MESH_H_PIPE_EPOCH) & 1u;
    mesh_words_add(w, mesh_flight_word(me, parity), 1);
    mesh_words_fence(w);
    return parity;
}
/* The push is over (its commit, if any, is ordered before: release). */
static inline void mesh_pipe_flight_end(const mesh_words *w, uint32_t me, uint32_t parity) {
    mesh_words_add(w, mesh_flight_word(me, parity), UINT32_MAX);
}

/* Pushes rec (len bytes) into slot c's inbox, on the CPU side (host code:
 * wasm lanes, native workers): 1 pushed, 0 full or busy (try again later), -e. */
static inline int mesh_pipe_push(const mesh_words *w, uint32_t me, uint32_t c, const uint8_t *rec, size_t len) {
    if (me >= MESH_PROG_SLOTS) return -MESH_PIPE_ERR_NOT_CONNECTED;
    uint32_t parity = mesh_pipe_flight_begin(w, me);
    mesh_pipe_inbox in;
    int r = mesh_pipe_target(w, me, c, &in);
    if (r == 0) {
        mesh_pipe_ring ring = mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_CPU);
        uint32_t p;
        if (len != ring.rec) r = -MESH_PIPE_ERR_SIZE;
        else if (!mesh_pipe_ring_reserve_push(&ring, w, me, &p)) r = 0;
        else {
            mesh_words_write(w, mesh_pipe_ring_cell(&ring, p), rec, len);
            mesh_words_store(w, mesh_pipe_ring_sum(&ring, p), mesh_pipe_rec_sum_at(rec, len, p));
            mesh_pipe_ring_commit_push(&ring, w, p);
            r = 1;
        }
    }
    mesh_pipe_flight_end(w, me, parity);
    return r == -MESH_PIPE_ERR_BUSY ? 0 : r;
}

/* Pops the next record of slot me's own inbox into out (its PP_IN_REC bytes,
 * len): 1 popped, 0 empty, -e. */
static inline int mesh_pipe_pop(const mesh_words *w, uint32_t me, uint8_t *out, size_t len) {
    mesh_pipe_inbox in;
    int r = mesh_pipe_target(w, me, me, &in);
    if (r == -MESH_PIPE_ERR_BUSY) return 0;
    if (r < 0) return r;
    if (len != in.rec) return -MESH_PIPE_ERR_SIZE;
    mesh_pipe_ring ring;
    uint32_t t;
    if (!mesh_pipe_inbox_take(&in, w, out, len, &ring, &t)) {
        mesh_pipe_ring cpu = mesh_pipe_inbox_side(&in, MESH_PIPE_SIDE_CPU);
        mesh_words_add(w, cpu.base + MESH_PIPE_PI_EMPTY, 1);
        return 0;
    }
    mesh_pipe_ring_commit_pop(&ring, w, t);
    return 1;
}

#endif
