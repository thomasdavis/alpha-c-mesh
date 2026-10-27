/* @ajax/mesh-jobs-out: what the job node asks of the world (jobs.rs `Out`), and `durable`: the
 * node's outputs made durable in order. Every PERSIST is appended to the log, and the log is synced
 * before the first SEND or ANSWER after it. The first append or sync that fails stops everything:
 * the outputs before it are returned with the error, and nothing after it is ever sent (fail-stop,
 * review finding 4). */
#ifndef MESH_JOBS_OUT_H
#define MESH_JOBS_OUT_H

#include "mesh/jobs/frame.h"
#include "mesh/jobs/id.h"
#include "mesh/jobs/log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MJ_OUT_SEND,       /* to, frame */
    MJ_OUT_PUSH_BLOB,  /* blobd: push job's blob `hash` (in this member's store or memory) to member `to` */
    MJ_OUT_FETCH_BLOB, /* blobd: fetch blob `hash` from the holders (else any member), keep it durably */
    MJ_OUT_BLOB_DONE,  /* blobd: job's blob `hash` needs no more pushes from here: its bytes may leave memory */
    MJ_OUT_PERSIST,    /* append `log` to the log and sync, before the sends that follow it */
    MJ_OUT_RUN,        /* the local driver: run lease `run` */
    MJ_OUT_CUT,        /* the local driver: drop lease `lease`'s items from `cut` on (0: all) */
    MJ_OUT_ANSWER,     /* a change's answer (after its commit), by the caller's token: ok, text */
    MJ_OUT_LOG,        /* a log line: text */
} mj_out_kind;

typedef struct {
    mj_out_kind kind;
    uint32_t to;
    mj_frame frame;
    mj_job_id job;
    mj_hash hash;
    uint32_t holders;
    mj_log log;
    mj_lease_msg run;
    uint64_t lease, cut;
    uint64_t token;
    bool ok;
    char *text;
} mj_out;

typedef struct {
    mj_out *v;
    size_t n, cap;
} mj_outs;

void mj_out_free(mj_out *o);
void mj_outs_push(mj_outs *v, mj_out o); /* takes o */
void mj_outs_append(mj_outs *dst, mj_outs *src); /* moves src's outputs (src is empty after) */
void mj_outs_free(mj_outs *v);

/* Constructors (they take what they're given). */
mj_out mj_out_send(uint32_t to, mj_frame f);
mj_out mj_out_push_blob(uint32_t to, mj_job_id job, mj_hash hash);
mj_out mj_out_fetch_blob(mj_job_id job, mj_hash hash, uint32_t holders);
mj_out mj_out_blob_done(mj_job_id job, mj_hash hash);
mj_out mj_out_persist(mj_log l);
mj_out mj_out_run(mj_lease_msg l);
mj_out mj_out_cut(uint64_t lease, uint64_t cut);
mj_out mj_out_answer(uint64_t token, bool ok, char *text);
mj_out mj_out_log(char *text);

/* Where the node's log goes (jobd's store; a test's faulty disk). Each returns false with *err (the
 * caller frees it) on failure. */
typedef struct {
    void *ctx;
    bool (*append)(void *ctx, const mj_log *l, char **err);
    bool (*sync)(void *ctx, char **err);
} mj_disk;

/* Takes `outs`: returns, in order, the outputs to act on (every PERSIST written and dropped). On a
 * failure *ok is false and *err says why; what follows the failure is dropped. */
mj_outs mj_durable(mj_outs *outs, const mj_disk *disk, bool *ok, char **err);

#endif
