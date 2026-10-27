/* @ajax/mesh-sched-place: the scheduler's placement pass (sched.rs place): Nomad's feasibility then
 * score, per program in a fixed order (priority, anchors before what is colocated with them, name;
 * ties go to the lower system id, so every leader computes the same answer from the same inputs).
 * A valid placement is kept: place only fills what is missing (a new program, a replica on a member
 * down for replace_ms, a member that no longer fits). Moving a running program for a better score is
 * the rebalancer's (mesh-sched-tracker). Also the lockstep group's member choice. */
#ifndef MESH_SCHED_PLACE_H
#define MESH_SCHED_PLACE_H

#include "mesh/fleet_doc.h"
#include "mesh/member_list.h"
#include "mesh/sched_member.h"
#include "mesh/sched_score.h"
#include "mesh/sched_weights.h"

#include <stdbool.h>
#include <stdint.h>

/* What the scheduler reads: the committed document, the members (index = id), the measured
 * performance per (program, member). */
typedef struct {
    const mfd_doc *doc;
    const msm_member *members;
    size_t nmembers;
    const msm_perfs *perf;
    const msw_weights *w;
} msp_input;

/* A member's verdict for a program: a score, or why not. */
typedef struct {
    uint32_t k;
    bool ok;
    msc_score_t score; /* ok */
    char *why;         /* !ok */
} msp_row;

/* Why a program is where it is: every member's verdict. */
typedef struct {
    char *program, *needs, *summary;
    msp_row *rows;
    size_t nrows;
    ml_opt chosen;
} msp_explain;

char *msp_explain_text(const msp_explain *e);
bool msp_score_of(const msp_explain *e, uint32_t k, double *score);
void msp_explain_free(msp_explain *e);

/* explains by program, in the document's order. */
typedef struct {
    msp_explain *v;
    size_t n;
} msp_explains;

void msp_explains_free(msp_explains *e);
const msp_explain *msp_explains_get(const msp_explains *e, const char *program);

/* The order programs are placed in: names into a new list. */
mt_strs msp_order(const mfd_doc *doc);
/* The members a placement puts a program on (none: every member that can run it). */
ml_list msp_members_of_at(const ml_opt *at, const msm_member *ms, size_t nms, const msn_needs *n);

/* A placement pass: the document with every program's placement made valid, the changes, and every
 * program's explain. A program being migrated (from) is left to the leader. */
mfd_doc msp_place(const msp_input *in, mt_strs *changes, msp_explains *explains);

/* The member set a group should have now (every eligible up member matching group_members;
 * group_size > 0: the best scoring that many, grown for link RTT among them), in system order, and
 * why; false when no member is eligible. */
bool msp_group_choice(const msp_input *in, const mfd_group *g, ml_list *members, char **why);
/* The group's score: the sum of its members' scores (a member that cannot run it counts 0). */
double msp_group_score(const msp_input *in, const mfd_group *g, const ml_list *members);

/* ---- for the rebalancer (mesh-sched-tracker) */

/* The bookkeeping while a pass runs: programs, reservations, and the views and slots placed on
 * members that do not hold them yet. */
typedef struct {
    size_t n;
    uint32_t *placed, *mem, *view_kb, *nnew;
    float *cpu;
} msp_books;

msp_books msp_books_new(size_t n);
void msp_books_free(msp_books *b);
void msp_books_add(msp_books *b, uint32_t k, const msn_needs *n);
/* Every member's verdict for program name (current: its placement now; anchor: NULL or the anchor's
 * members). */
void msp_rows(const msp_input *in, const char *name, const ml_list *current, const msp_books *books, const ml_list *anchor, msp_row **rows, size_t *nrows);
void msp_rows_free(msp_row *rows, size_t n);
/* The feasible rows' (k, total), best first (ties: the lower id). */
size_t msp_sorted_ok(const msp_row *rows, size_t n, uint32_t *ks, double *totals);

#endif
