/* @ajax/mesh-jobs-merge: a member's answer to a new leader's pull (jobs.rs `Answer`) and the merge
 * of a majority's answers (`merge`), which holds every committed entry because any two majorities
 * meet. Committed tables are applied first, the latest commit point first; then the entries not
 * committed yet, the latest leadership's first. The table is a union where an item's first result
 * wins, so the order decides between two results of one item: a committed result is never
 * overwritten by an uncommitted one, and between entries the later leadership's wins (review
 * finding 5). */
#ifndef MESH_JOBS_MERGE_H
#define MESH_JOBS_MERGE_H

#include "mesh/jobs/log.h"
#include "mesh/jobs/op.h"
#include "mesh/jobs/table.h"
#include "mesh/omap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t seq;     /* the next part expected; a part out of sequence spoils the answer */
    bool spoilt;
    uint64_t commit_lid, commit_n;
    mj_table committed;
    momap *entries;   /* (leadership, number) -> mj_ops: its entries not committed yet */
} mj_answer;

void mj_answer_init(mj_answer *a);
void mj_answer_free(mj_answer *a);
/* The ops of entry (lid, n), created empty if absent. */
mj_ops *mj_answer_entry(mj_answer *a, uint64_t lid, uint64_t n);
/* A member's own answer, without frames (the new leader's). */
mj_answer mj_answer_of(const mj_replica *r);

/* The merge of answers[0..n) given in member-id order. */
mj_table mj_merge(mj_answer *const *answers, size_t n);

#endif
