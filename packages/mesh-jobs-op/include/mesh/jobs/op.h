/* @ajax/mesh-jobs-op: an edit of the job table (jobs.rs `Op`), an item's committed result (`Res`)
 * and a payload's reference (`BlobRef`), and the binary encoding of ops (`put_op`, `get_op`,
 * `encode_ops`, `decode_ops`, `parts`).
 *
 * Every op is idempotent, and applying two tables' ops in any order gives their union (an item's
 * first result wins): see @ajax/mesh-jobs-table. On the wire an op is a u8 tag (its kind's number
 * below) and its fields. */
#ifndef MESH_JOBS_OP_H
#define MESH_JOBS_OP_H

#include "mesh/jobs/id.h"
#include "mesh/jobs/ranges.h"
#include "mesh/jobs/spec.h"
#include "mesh/jobs/wire.h"
#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A delta, a snapshot part: at most this many bytes of ops per frame. */
#define MESH_JOBS_PART_BYTES ((size_t)1 << 20)

/* An item's payload, off the log: its sha256, its length, and the members that acknowledged a
 * durable copy (bit k; the leader that committed it too). */
typedef struct {
    mj_hash hash;
    uint32_t len, holders;
} mj_blob_ref;

/* An item's result as committed. */
typedef struct {
    uint64_t lease;
    uint32_t member, result;
    uint8_t *out;
    size_t out_len;
    bool has_blob;
    mj_blob_ref blob;
} mj_res;

void mj_res_free(mj_res *r);
mj_res mj_res_clone(const mj_res *r);
bool mj_res_eq(const mj_res *a, const mj_res *b);
bool mj_blob_ref_eq(const mj_blob_ref *a, const mj_blob_ref *b);

typedef struct {
    uint32_t member;
    uint64_t n;
} mj_tally;

typedef enum {
    MJ_OP_SUBMIT = 1,
    MJ_OP_CANCEL = 2,
    MJ_OP_DONE = 3,
    MJ_OP_DONE_RUN = 4, /* items [s, e) done with result 0 and no bytes, by one lease */
    MJ_OP_FAIL = 5,
    MJ_OP_FAILS = 6,
    MJ_OP_DUPES = 7,
    MJ_OP_FINISH = 8,
    MJ_OP_FORGET = 9,
    MJ_OP_FOLD = 10,    /* every item from the job's first to `upto` is done; tally: the folded runs' items per member */
    MJ_OP_TONE = 11,    /* the job's presentation word := tone (change seq: the highest wins) */
} mj_op_kind;

/* One op; the fields its kind uses (the rest are zero). */
typedef struct {
    mj_op_kind kind;
    mj_job_id job;     /* every kind (Submit: spec.id) */
    mj_spec spec;      /* SUBMIT */
    char *why;         /* CANCEL, FAIL */
    uint64_t item;     /* DONE, FAIL, FAILS */
    mj_res res;        /* DONE */
    uint64_t s, e, lease; /* DONE_RUN */
    uint32_t member;   /* DONE_RUN */
    uint32_t tries;    /* FAIL, FAILS */
    uint64_t n;        /* DUPES */
    uint64_t at_ms;    /* FINISH */
    uint32_t state;    /* FINISH */
    uint64_t upto;     /* FOLD */
    mj_tally *tally;   /* FOLD */
    size_t ntally;
    uint32_t seq, tone; /* TONE */
} mj_op;

void mj_op_free(mj_op *o);
mj_op mj_op_clone(const mj_op *o);
bool mj_op_eq(const mj_op *a, const mj_op *b);
/* The job an op edits. */
static inline mj_job_id mj_op_job(const mj_op *o) { return o->kind == MJ_OP_SUBMIT ? o->spec.id : o->job; }

/* Constructors (they copy what they're given). */
mj_op mj_op_submit(const mj_spec *s);
mj_op mj_op_cancel(mj_job_id job, const char *why);
mj_op mj_op_done(mj_job_id job, uint64_t item, const mj_res *res);
mj_op mj_op_done_run(mj_job_id job, uint64_t s, uint64_t e, uint64_t lease, uint32_t member);
mj_op mj_op_fail(mj_job_id job, uint64_t item, uint32_t tries, const char *why);
mj_op mj_op_fails(mj_job_id job, uint64_t item, uint32_t tries);
mj_op mj_op_dupes(mj_job_id job, uint64_t n);
mj_op mj_op_finish(mj_job_id job, uint64_t at_ms, uint32_t state);
mj_op mj_op_forget(mj_job_id job);
mj_op mj_op_fold(mj_job_id job, uint64_t upto, const mj_tally *tally, size_t n);
mj_op mj_op_tone(mj_job_id job, uint32_t seq, uint32_t tone);

typedef struct {
    mj_op *v;
    size_t n, cap;
} mj_ops;

void mj_ops_push(mj_ops *v, mj_op o); /* takes o */
void mj_ops_free(mj_ops *v);
mj_ops mj_ops_clone(const mj_ops *v);
bool mj_ops_eq(const mj_ops *a, const mj_ops *b);
/* Moves every op of src onto the end of dst (src is empty after). */
void mj_ops_append(mj_ops *dst, mj_ops *src);

/* A payload reference: tag 0 (none) or 1, the hash, len, holders. */
void mj_w_blob(msb *w, bool has, const mj_blob_ref *b);
bool mj_rd_blob(mj_rd *r, bool *has, mj_blob_ref *b);

void mj_put_op(msb *w, const mj_op *o);
/* One op from r; on failure r->err says why ("job op 12?", "job op fold: 9 members", the spec's). */
bool mj_get_op(mj_rd *r, mj_op *o);
void mj_encode_ops(const mj_op *ops, size_t n, msb *out);
/* Ops until the bytes end; false with *err (the caller frees it). */
bool mj_decode_ops(const void *b, size_t n, mj_ops *out, char **err);
/* Ops read from r until it ends (appended to out); false on the first bad one. */
bool mj_rd_ops(mj_rd *r, mj_ops *out);

/* Ops encoded in parts of at most PART_BYTES each (an op is never split, so a part may run over by
 * its last op); always at least one part (empty when there are no ops). */
typedef struct {
    msb *v;
    size_t n;
} mj_parts;
mj_parts mj_parts_of(const mj_op *ops, size_t n);
void mj_parts_free(mj_parts *p);

#endif
