/* @ajax/mesh-jobs-sched: the fleet scheduler's model as the jobs' dispatcher reads it (jobs.rs
 * `SchedCx`): every member (index = id, with the owner policy's busy marks), each program's needs
 * from the desired state, the measured performance per (program, member), and the weights. The fleet
 * thread refreshes it every second and hands it to the node (mj_node_set_sched) as an mj_sched, whose
 * judge is F4's `sched::filter` and `score_terms` (@ajax/mesh-sched-score). */
#ifndef MESH_JOBS_SCHED_H
#define MESH_JOBS_SCHED_H

#include "mesh/jobs/select.h"
#include "mesh/sched_member.h"
#include "mesh/sched_needs.h"
#include "mesh/sched_weights.h"

#include <stddef.h>

typedef struct {
    msm_member *members; /* index = member id */
    size_t nmembers;
    char **need_names;   /* program -> needs */
    msn_needs *needs;
    size_t nneeds;
    msm_perfs perf;      /* (program, member) -> measured performance */
    msw_weights w;
} mjs_cx;

void mjs_cx_free(mjs_cx *cx);
/* The needs of program name in cx (NULL if none). */
const msn_needs *mjs_needs(const mjs_cx *cx, const char *name);
/* An mj_sched over cx, which it takes (for mj_node_set_sched, or mj_select). */
mj_sched *mjs_sched(mjs_cx cx);

#endif
