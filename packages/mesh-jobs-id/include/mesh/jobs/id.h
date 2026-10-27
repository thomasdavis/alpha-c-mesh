/* @ajax/mesh-jobs-id: a job's id and a payload's hash, and their text forms (jobs.rs).
 *
 * A job id is `t.n`: the n-th job submitted in fleet term t (term in the high 32 bits). A payload's
 * hash is its sha256, named in lowercase hex. */
#ifndef MESH_JOBS_ID_H
#define MESH_JOBS_ID_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef uint64_t mj_job_id;

typedef struct {
    uint8_t b[32];
} mj_hash;

#define MJ_JOB_WORD_MAX 32 /* "4294967295.4294967295" and its NUL fit */

/* `t.n` into out. */
void mj_job_word(mj_job_id id, char out[MJ_JOB_WORD_MAX]);
/* `t.n` (t a u64, shifted up 32 bits; n a u32); false when it isn't one. */
bool mj_parse_job(const char *s, mj_job_id *out);
/* 64 lowercase hex digits into out. */
void mj_hex(const mj_hash *h, char out[65]);
/* Exactly 64 hex digits (each pair as Rust's u8::from_str_radix reads it); false otherwise. */
bool mj_unhex(const char *s, mj_hash *out);

static inline bool mj_hash_eq(const mj_hash *a, const mj_hash *b) { return memcmp(a->b, b->b, 32) == 0; }
/* The hash as four big-endian words: ordered as the bytes are (a map key). */
static inline void mj_hash_words(const mj_hash *h, uint64_t w[4]) {
    for (int i = 0; i < 4; i++) {
        uint64_t x = 0;
        for (int j = 0; j < 8; j++) x = x << 8 | h->b[8 * i + j];
        w[i] = x;
    }
}
static inline mj_hash mj_hash_of_words(const uint64_t w[4]) {
    mj_hash h;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) h.b[8 * i + j] = (uint8_t)(w[i] >> (56 - 8 * j));
    return h;
}

#endif
