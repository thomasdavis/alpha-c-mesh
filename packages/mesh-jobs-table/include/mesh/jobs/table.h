/* @ajax/mesh-jobs-table: the job table (jobs.rs `Job`, `Table`): every job with what is known of
 * it, changed only by applying ops (@ajax/mesh-jobs-op).
 *
 * The table is a join-semilattice: every op is idempotent and applying two tables' ops in either
 * order gives the same table, their union, in which an item's first result wins. That is what lets
 * a new leader merge a majority's tables without matching logs. */
#ifndef MESH_JOBS_TABLE_H
#define MESH_JOBS_TABLE_H

#include "mesh/jobs/id.h"
#include "mesh/jobs/op.h"
#include "mesh/jobs/ranges.h"
#include "mesh/jobs/spec.h"
#include "mesh/omap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_JOBS_FIN_DONE 1u
#define MESH_JOBS_FIN_CANCELLED 2u
#define MESH_JOBS_FIN_DEADLINE 3u

/* A run: done items [start, end) with no result kept, by one lease on one member. */
typedef struct {
    uint64_t end, lease;
    uint32_t member;
} mj_run;

/* An item failed for good. */
typedef struct {
    uint32_t tries;
    char *why;
} mj_failed;

typedef struct {
    mj_spec spec;
    mj_ranges done;     /* items done (whatever the mode) */
    momap *results;     /* item -> mj_res: mode collect, every done item's; nonzero, those not 0 */
    momap *runs;        /* start -> mj_run: done items with no result kept */
    momap *failed;      /* item -> mj_failed: failed for good */
    momap *fails;       /* item -> uint32_t: tries failed so far (max-merged) */
    uint64_t dupes;
    char *cancel;       /* NULL: not cancelled */
    bool finished;
    uint64_t finish_at; /* unix ms */
    uint32_t finish_state; /* FIN_* */
    momap *by_member;   /* member -> uint64_t items done (from the done ops as they apply) */
    /* The runs folded away: every item from the job's first to fold_end is done, and `folded` has, per
     * member, the items of the runs dropped (review finding 7). */
    uint64_t fold_end;
    momap *folded;      /* member -> uint64_t */
    uint32_t tone_seq, tone; /* the presentation word and its change count (the highest count wins) */
} mj_job;

mj_job *mj_job_new(const mj_spec *spec); /* copies spec */
void mj_job_free(mj_job *j);
mj_job *mj_job_clone(const mj_job *j);
bool mj_job_eq(const mj_job *a, const mj_job *b);

uint64_t mj_job_total(const mj_job *j);
bool mj_job_settled(const mj_job *j, uint64_t x);
/* The lease that ran done item x; false if it has none on record. */
bool mj_job_lease_of(const mj_job *j, uint64_t x, uint64_t *lease);
bool mj_job_active(const mj_job *j);
/* Every item done or failed (an endless job never is). */
bool mj_job_complete(const mj_job *j);
/* done, done-with-failures, cancelled, deadline, cancelling, running. */
const char *mj_job_state(const mj_job *j);
/* Items not done and not failed. */
mj_ranges mj_job_left(const mj_job *j);
/* Small-map helpers: a member's count (0 when absent). */
uint64_t mj_count_get(const momap *m, uint32_t member);

typedef struct {
    momap *jobs;           /* id -> mj_job * */
    mj_job_id *forgotten;  /* jobs forgotten (`job forget`): their ops are ignored */
    size_t nforgotten, capforgotten;
} mj_table;

void mj_table_init(mj_table *t);
void mj_table_free(mj_table *t);
mj_table mj_table_clone(const mj_table *t);
bool mj_table_eq(const mj_table *a, const mj_table *b);
mj_job *mj_table_get(const mj_table *t, mj_job_id id);
bool mj_table_forgot(const mj_table *t, mj_job_id id);

void mj_table_apply(mj_table *t, const mj_op *op);
/* The whole table as ops (a snapshot, a pull's answer), appended to out. */
void mj_table_ops(const mj_table *t, mj_ops *out);
/* The next job id of term `term`. */
mj_job_id mj_table_next_id(const mj_table *t, uint64_t term);

/* Iteration in id order: mj_job *j; uint64_t id; for (j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)). */
mj_job *mj_table_first(const mj_table *t, uint64_t *id);
mj_job *mj_table_next(const mj_table *t, uint64_t *id);

#endif
