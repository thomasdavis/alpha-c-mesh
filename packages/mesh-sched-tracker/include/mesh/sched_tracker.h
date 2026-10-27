/* @ajax/mesh-sched-tracker: the rebalancer (sched.rs Tracker) and the leader's moves.
 *
 * A program moves only when its best alternative scores `improve` (25 %) better than where it is, for
 * hold_ms in a row, at least dwell_ms after its last move, one migration at a time and at most per_min
 * a minute. The owner policy (HTCondor) marks a member busy while other tenants load it (GPU share under
 * owner_share or CPU PSI over owner_psi for owner_ms; cleared with hysteresis): a busy member's score
 * drops by `owner`, so movable work leaves it. A lockstep group is never moved: the scheduler proposes
 * a re-form. A move is in the document (source, target, the version that started it), so a new leader
 * takes over a migration the old one started. */
#ifndef MESH_SCHED_TRACKER_H
#define MESH_SCHED_TRACKER_H

#include "mesh/fleet_doc.h"
#include "mesh/sched_place.h"

#include <stdbool.h>
#include <stdint.h>

/* A move the leader carries out: programs (an anchor and what is colocated with it, in stop order:
 * producers first) from one member to another. */
typedef struct {
    mt_strs programs;
    uint32_t from, to;
    bool state;  /* with the view (migrate = "state"), else a fresh start on to */
    char *why;
    bool forced; /* an operator's pin (no hysteresis) */
} mst_move;

void mst_move_free(mst_move *m);
mst_move mst_move_clone(const mst_move *m);
bool mst_move_eq(const mst_move *a, const mst_move *b);

typedef struct mst_tracker mst_tracker;

mst_tracker *mst_new(void);
void mst_free(mst_tracker *t);
/* The last step's verdicts (for `sched`). */
const mt_strs *mst_notes(const mst_tracker *t);

/* The owner policy: sets busy on the members (they are written). */
void mst_owner(mst_tracker *t, msm_member *members, size_t n, uint64_t now, const msw_weights *w);
/* Migrations in the last minute. */
uint32_t mst_recent(mst_tracker *t, uint64_t now);
/* A move was started (the budget and the dwell count from now). */
void mst_moved(mst_tracker *t, const mst_move *m, uint64_t now);
/* One rebalancing step: true with *out the move to make now (busy: a migration is in flight). */
bool mst_step(mst_tracker *t, const msp_input *in, uint64_t now, bool busy, mst_move *out);
/* A re-form the group should have (held hold_ms): true with the group, members and why to propose. */
bool mst_group_step(mst_tracker *t, const msp_input *in, uint64_t now, char **group, ml_list *members, char **why);

/* The programs that move with name from `from` (it and, transitively, what is colocated with it
 * there), producers first. */
mt_strs mst_unit(const mfd_doc *doc, const char *name, uint32_t from);
/* The document after a move starts (as version since). */
mfd_doc mst_start_move(const mfd_doc *doc, const mst_move *m, uint64_t since);
/* The migrations the document has in flight, as moves with the version that started each. */
typedef struct {
    mst_move *moves;
    uint64_t *since;
    size_t n;
} mst_resumed;
mst_resumed mst_resume_moves(const mfd_doc *doc);
void mst_resumed_free(mst_resumed *r);

/* What a member's reconciler did with a program placed on it by a move: at document version, it ran
 * there (ok, the migration's gap) or it did not. F_BEAT carries them. */
typedef struct {
    char *name;
    uint64_t version;
    bool ok;
    uint32_t gap_us;
} mst_outcome;

/* A move's verdict at the leader once the target reported every program of it at the move's version
 * or later (or the target was lost, or waited_ms passed limit_ms): true with *ok, *why and *gap_ms
 * (< 0: none); false while it is under way. Only the moved programs count. */
bool mst_mig_verdict(const mst_move *m, uint64_t since, bool target_up, const mst_outcome *outs, size_t nouts, const char *reason, uint64_t waited_ms,
                     uint64_t limit_ms, bool *ok, char **why, double *gap_ms);
/* The document after a move: from released (done) or the unit put back (reverted). */
mfd_doc mst_end_move(const mfd_doc *doc, const mst_move *m, bool ok, const char *why);

#endif
