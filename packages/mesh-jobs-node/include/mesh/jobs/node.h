/* @ajax/mesh-jobs-node: a meshd's job node, sans IO (jobs.rs `Node`): its replica of the fleet's job
 * table, the leader's dispatcher when it leads, and its side of the leases. jobd runs one per meshd
 * over the peer links, the driver and a persisted log (@ajax/mesh-jobd); the tests run several over a
 * simulated network.
 *
 * The node never does IO: every input (a view of the fleet, a frame, a finished item, a stored or
 * acknowledged blob, a request, a tick) returns outputs (@ajax/mesh-jobs-out) for the caller to make
 * durable (`mj_durable`) and act on, in order.
 *
 * The struct is open so jobd's views and the tests can read it; change it only through the
 * functions. */
#ifndef MESH_JOBS_NODE_H
#define MESH_JOBS_NODE_H

#include "mesh/jobs/frame.h"
#include "mesh/jobs/id.h"
#include "mesh/jobs/log.h"
#include "mesh/jobs/merge.h"
#include "mesh/jobs/out.h"
#include "mesh/jobs/rate.h"
#include "mesh/jobs/req.h"
#include "mesh/jobs/select.h"
#include "mesh/jobs/table.h"
#include "mesh/omap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A member re-sends a result the table does not show yet after this long (ms). */
#define MESH_JOBS_RESEND_MS 3000u
/* A member's F_JOB_AVAIL every second (and on any change). */
#define MESH_JOBS_AVAIL_MS 1000u
/* A member whose lease expired gets no new lease of that program for this long (or 4 x the dead time). */
#define MESH_JOBS_HOLDOFF_MS 10000u
/* An expired lease's items each count a try only up to this many items. */
#define MESH_JOBS_EXPIRY_TRIES_MAX 10000u
/* `job results` lines per request, at most. */
#define MESH_JOBS_RESULTS_MAX 100000u
/* A submit for a program no member reports yet waits this long for one. */
#define MESH_JOBS_SUBMIT_WAIT_MS 2000u

typedef struct {
    uint32_t me, n, quorum;
    uint64_t dead_ms;
} mj_cfg;

/* The fleet as the jobs thread sees it: the fleet term, the leader, whom it hears (alive) and whom it
 * is linked to. A single meshd: term 1, itself. */
typedef struct {
    uint64_t term;
    bool has_leader;
    uint32_t leader;
    uint32_t alive, linked;
} mj_view;

static inline bool mj_view_eq(const mj_view *a, const mj_view *b) {
    return a->term == b->term && a->has_leader == b->has_leader && (!a->has_leader || a->leader == b->leader) && a->alive == b->alive &&
           a->linked == b->linked;
}

/* A lease, as the leader tracks it. */
typedef struct {
    uint64_t id;
    mj_job_id job;
    uint32_t member;
    char *program;
    mj_ranges out; /* items not reported yet */
    uint64_t size, issued, deadline;
} mj_lease;

typedef struct {
    mj_ranges fresh, retry;
} mj_queue;

/* A small ordered map keyed by (member, program): the leader's rates and hold-offs. */
typedef struct {
    uint32_t member;
    char *program;
    mj_rate rate;    /* rates */
    uint64_t until;  /* holdoff */
} mj_mp;

typedef struct {
    mj_mp *v;
    size_t n, cap;
} mj_mpmap;

mj_mp *mj_mpmap_get(const mj_mpmap *m, uint32_t member, const char *program);
mj_mp *mj_mpmap_put(mj_mpmap *m, uint32_t member, const char *program); /* zeroed if new */

/* The leader's dispatcher (soft state: a new leader rebuilds it). */
typedef struct {
    momap *leases;          /* id -> mj_lease */
    momap *queues;          /* job -> mj_queue */
    mj_member_progs progs;  /* per member: the job programs it runs (F_JOB_AVAIL) */
    mj_mpmap rates;         /* (member, program) -> rate */
    mj_mpmap holdoff;       /* (member, program) -> until: no new lease of it there */
    uint64_t next_lease;
    momap *picks;           /* job -> mj_picks: the last select per job (for `job status`) */
} mj_disp;

typedef struct {
    uint64_t n, token;
    bool ok;
    char *text;
} mj_wait;

typedef struct {
    mj_submit s;
    uint64_t token, until;
} mj_held_submit;

typedef struct {
    uint32_t member;
    mj_held h;
} mj_adopt;

typedef struct {
    uint32_t from;
    mj_item_res r;
} mj_parked;

typedef struct {
    mj_parked *v;
    size_t n, cap;
} mj_parked_list;

typedef struct {
    uint64_t lid, term;
    bool active;
    /* Pulling: who answered (wholly), the answers, the leases members hold. */
    uint32_t responded;
    mj_answer *answers[MJ_MEMBERS];
    mj_adopt *adopt;
    size_t nadopt, capadopt;
    uint64_t pull_at;
    uint32_t heard_mask;
    uint64_t heard[MJ_MEMBERS]; /* when a part of a member's answer last arrived */
    mj_table working;
    uint64_t n; /* the last entry number used */
    uint64_t *acked; /* per member (cfg.n) */
    uint64_t commit;
    uint32_t snap_due;
    mj_ops ops;  /* ops of this tick, not in a delta yet */
    mj_wait *waits;
    size_t nwaits, capwaits;
    mj_disp disp;
    momap *dupes; /* job -> uint64_t duplicates counted here */
    mj_held_submit *held;
    size_t nheld, capheld;
    momap *parked;   /* (job, hash words) -> mj_parked_list: results waiting for their blob here */
    momap *fetch_at; /* (job, hash words) -> uint64_t: when each parked blob was last asked for */
} mj_lead;

/* A payload result before its copies are durable. */
typedef struct {
    mj_item_res r;
    uint32_t acked, refused;
    uint32_t sent_mask;
    uint64_t sent[MJ_MEMBERS]; /* when each member was last pushed to */
} mj_staged;

typedef struct {
    mj_item_res r;
    uint64_t at;
} mj_outbox_entry;

typedef struct {
    mj_cfg cfg;
    mj_view view;
    mj_replica rep; /* this member's durable state (its log replays to it) */
    bool has_lid_from;
    uint32_t lid_from; /* the member that leads rep.lid */
    uint64_t next;
    bool has_staging;
    uint64_t staging_n;
    uint32_t staging_seq;
    mj_table staging; /* a table arriving in parts */
    uint64_t gap_at;
    mj_lead *lead;
    uint64_t behind;
    /* The member side. */
    momap *held;   /* lease -> mj_held */
    momap *outbox; /* (job, item, lease) -> mj_outbox_entry */
    mj_prog *progs;
    size_t nprogs;
    uint64_t avail_at;
    bool avail_dirty;
    uint64_t prune_at;
    mj_rate rate;
    mj_frame *lo; /* frames to this member itself (the leader is a member too) */
    size_t nlo, lo_head, caplo;
    mj_sched *sched; /* the fleet scheduler's model (NULL: a single meshd); owned */
    momap *staged; /* (job, item, lease) -> mj_staged */
    momap *have;   /* (job, hash words): the jobs' blobs this member's store holds durably */
} mj_node;

/* A node from its replayed log (takes rep). */
mj_node *mj_node_new(mj_cfg cfg, mj_replica rep);
void mj_node_free(mj_node *n);

const mj_table *mj_node_committed(const mj_node *n);
/* The fleet scheduler's model (every second, from the fleet thread); the node takes it. */
void mj_node_set_sched(mj_node *n, mj_sched *cx);
/* The leader's last member choice for a job (NULL: not leading, or nothing to lease). */
const mj_picks *mj_node_picks(const mj_node *n, mj_job_id job);
bool mj_node_leading(const mj_node *n);
bool mj_node_active(const mj_node *n);
uint64_t mj_node_lid(const mj_node *n);
uint64_t mj_node_commit_n(const mj_node *n);
/* The leader's working table (results not committed yet included), or NULL. */
const mj_table *mj_node_working(const mj_node *n);
/* The leader's leases (id -> mj_lease), or NULL. */
const momap *mj_node_leases(const mj_node *n);
const mj_mpmap *mj_node_rates(const mj_node *n);
bool mj_node_queued(const mj_node *n, mj_job_id job, uint64_t *count);
double mj_node_my_rate(const mj_node *n);
size_t mj_node_unconfirmed(const mj_node *n);
size_t mj_node_staged(const mj_node *n);
size_t mj_node_parked(const mj_node *n);
bool mj_node_has_blob(const mj_node *n, mj_job_id job, const mj_hash *h);

/* The fleet's view changed (or the periodic refresh). */
mj_outs mj_node_set_view(mj_node *n, mj_view v, uint64_t now, uint64_t unix_ms);
/* A frame from member `from` (taken). */
mj_outs mj_node_recv(mj_node *n, uint32_t from, mj_frame f, uint64_t now, uint64_t unix_ms);
/* A finished item from the local driver (taken); a payload's blob already handed to blobd. */
mj_outs mj_node_result(mj_node *n, mj_item_res r, uint64_t now);
/* Job's blob `hash` is durable in this member's store. */
mj_outs mj_node_blob_stored(mj_node *n, mj_job_id job, mj_hash hash, uint64_t now, uint64_t unix_ms);
/* Job's blob `hash` left this member's store. */
void mj_node_blob_gone(mj_node *n, mj_job_id job, const mj_hash *hash);
/* Member `from` acknowledged job's blob durable (ok), or its store refused it. */
void mj_node_blob_acked(mj_node *n, uint32_t from, mj_job_id job, mj_hash hash, bool ok);
/* The job programs running here (from the driver; taken). */
void mj_node_set_progs(mj_node *n, mj_prog *progs, size_t count);
/* A change asked for (taken); answered by token after its commit. */
mj_outs mj_node_request(mj_node *n, mj_req req, uint64_t token, uint64_t now, uint64_t unix_ms);
/* The periodic work: the leader's dispatch, deltas and commits; a member's results and availability. */
mj_outs mj_node_tick(mj_node *n, uint64_t now, uint64_t unix_ms);

/* This member's answer to a pull (numbered F_JOB_HAVE parts), as frames (for tests). */
void mj_node_answer(const mj_node *n, uint64_t lid, mj_frame **out, size_t *count);
/* The leader's handling of results from `from` (taken; for tests: jobs.rs's tests call it directly). */
void mj_node_on_results(mj_node *n, uint32_t from, mj_item_res *items, size_t count, uint64_t now, uint64_t unix_ms, mj_outs *out);

#endif
