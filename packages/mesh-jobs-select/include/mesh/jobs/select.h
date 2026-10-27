/* @ajax/mesh-jobs-select: who gets a job's leases (jobs.rs `select`, `Pick`, `SchedCx`).
 *
 * A member is eligible if it is alive, in the job's members, runs one of the job's programs, and the
 * fleet scheduler (F4: `sched::filter`, `score_terms`) passes and scores it. The scheduler is an
 * interface here (`mj_sched`): @ajax/mesh-jobs-sched implements it over the fleet's scheduler model;
 * without one (a single meshd) a member that runs the program and is alive is eligible, unscored.
 * HTCondor's backoff: a member the owner policy holds busy gets no leases while an idle member runs
 * the program. Picks come best score first, ties to the lower id, rejected members last. */
#ifndef MESH_JOBS_SELECT_H
#define MESH_JOBS_SELECT_H

#include "mesh/jobs/frame.h"
#include "mesh/jobs/spec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MJ_MEMBERS 32 /* member ids are bits of a u32 */

/* A score and its explanation (sched::Score::explain). */
typedef struct {
    double total;
    char *explain;
} mj_score;

/* The explanation of an unscored verdict (sched::Score::default().explain()). */
#define MJ_SCORE_DEFAULT_EXPLAIN "0.000 = perf 0.00 load 0.00 mem 0.00 spread 0.00 (, cpu after 0.00)"

/* One member's standing for a job's next lease. */
typedef struct {
    uint32_t member;
    bool ok;        /* eligible (score) or rejected (why) */
    mj_score score;
    char *why;
    bool busy;      /* eligible but held busy by the owner policy (only floor-sized leases) */
} mj_pick;

typedef struct {
    mj_pick *v;
    size_t n;
} mj_picks;

void mj_picks_free(mj_picks *p);
mj_picks mj_picks_clone(const mj_picks *p);
/* `pick system 0 eligible <explain>[ busy: ...]` or `pick system 3 rejected: <why>`. */
char *mj_pick_line(const mj_pick *p);
/* The pick of member m that is eligible, or NULL. */
const mj_pick *mj_picks_ok(const mj_picks *p, uint32_t m);

/* The job programs each member runs (from its F_JOB_AVAIL). */
typedef struct {
    bool has[MJ_MEMBERS];
    mj_prog *v[MJ_MEMBERS];
    size_t n[MJ_MEMBERS];
} mj_member_progs;

void mj_member_progs_free(mj_member_progs *p);
void mj_member_progs_set(mj_member_progs *p, uint32_t k, mj_prog *progs, size_t n); /* takes progs */
void mj_member_progs_remove(mj_member_progs *p, uint32_t k);

/* The fleet scheduler, as select asks it. judge(k, prog) is asked for an alive member k that runs
 * prog, when k < nmembers: 1 eligible (*score filled; *busy: the owner policy holds it), 0 rejected
 * (*why), -1 no model for it (no needs for prog: eligible, unscored). busy_why(k): why the owner
 * policy holds k busy (NULL if it doesn't); release frees ctx. */
typedef struct mj_sched {
    void *ctx;
    uint32_t nmembers;
    int (*judge)(void *ctx, uint32_t k, const char *prog, mj_score *score, bool *busy, char **why);
    char *(*busy_why)(void *ctx, uint32_t k);
    void (*release)(void *ctx);
} mj_sched;

/* Every member's verdict for job's next lease, best first. cx may be NULL (a single meshd). */
mj_picks mj_select(const mj_spec *job, uint32_t alive, const mj_member_progs *progs, const mj_sched *cx);

#endif
