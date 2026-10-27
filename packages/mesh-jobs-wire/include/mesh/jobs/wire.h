/* @ajax/mesh-jobs-wire: the binary encoding of the job table's ops, the job frames and the job log
 * (jobs.rs's `W` and `R`): little-endian u8/u32/u64, bytes as a u32 length then the bytes, strings as
 * UTF-8 bytes, ranges as a u32 count then (start, end) u64 pairs.
 *
 * The reader stops at its first failure and keeps the message (jobs.rs's exact text: "job frame:
 * short (3 of 8 bytes at 17)", "job frame: not UTF-8"); every later read fails too. */
#ifndef MESH_JOBS_WIRE_H
#define MESH_JOBS_WIRE_H

#include "mesh/jobs/ranges.h"
#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mj_w_u8(msb *w, uint8_t v);
void mj_w_u32(msb *w, uint32_t v);
void mj_w_u64(msb *w, uint64_t v);
void mj_w_bytes(msb *w, const void *b, size_t n);
void mj_w_str(msb *w, const char *s);
void mj_w_ranges(msb *w, const mj_ranges *r);

typedef struct {
    const uint8_t *b;
    size_t n, at;
    char *err; /* the first failure (owned), or NULL */
} mj_rd;

static inline mj_rd mj_rd_new(const void *b, size_t n) { return (mj_rd){(const uint8_t *)b, n, 0, NULL}; }
void mj_rd_free(mj_rd *r);                     /* frees err */
char *mj_rd_take_err(mj_rd *r);                /* the message, now the caller's */
bool mj_rd_fail(mj_rd *r, char *msg);          /* records msg (owned) unless one is there; false */
bool mj_rd_take(mj_rd *r, size_t n, const uint8_t **p);
bool mj_rd_u8(mj_rd *r, uint8_t *v);
bool mj_rd_u32(mj_rd *r, uint32_t *v);
bool mj_rd_u64(mj_rd *r, uint64_t *v);
bool mj_rd_bytes(mj_rd *r, uint8_t **b, size_t *n); /* a fresh copy (never NULL on success) */
bool mj_rd_str(mj_rd *r, char **s);                 /* checked UTF-8, NUL-terminated copy */
bool mj_rd_ranges(mj_rd *r, mj_ranges *out);
static inline bool mj_rd_done(const mj_rd *r) { return r->at == r->n; }

/* Rust's String::from_utf8 test: well-formed UTF-8 (no overlongs, surrogates or values past U+10FFFF). */
bool mj_utf8_ok(const uint8_t *b, size_t n);
/* Rust's String::from_utf8_lossy: each maximal invalid subsequence becomes U+FFFD. A new string. */
char *mj_utf8_lossy(const uint8_t *b, size_t n);

#endif
