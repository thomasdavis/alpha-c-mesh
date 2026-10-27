/* @ajax/mesh-reconcile: the reconciler (reconcile.rs). It makes this member's programs match the
 * committed desired state, on every commit and when the member (re)joins; it is the only thing that
 * changes the program set in a fleet. One pass:
 * 1. the plan (mesh-fleet-plan): unload what should not be here, load what is missing, upgrade on a hash
 *    mismatch (a rollback when the retained image is the one wanted), then run states and resets. An
 *    image comes from the local catalog when its identity matches, else from a member. A program this
 *    member has no image for is reported unsupported, not failed; a failure is retried 30 s later or at
 *    the next document. Programs migrating here are loaded stopped, take their views from the source
 *    (producers first), and start (consumers first);
 * 2. the lockstep group: a new epoch quiesces the program, sets the group words, resets it and asks the
 *    fleet thread to HELLO; a cycle inside the epoch quiesces it with rounds still mirrored, applies it
 *    and HELLOs only with the document's image and the cycle's task done here (review finding 2);
 * 3. on the leader, the placement requests.
 * Everything that touches the driver or the fleet thread goes through mrc_ops (the daemon wires it). */
#ifndef MESH_RECONCILE_H
#define MESH_RECONCILE_H

#include "mesh/fleet_doc.h"
#include "mesh/fleet_plan.h"
#include "mesh/peer_codec.h"
#include "mesh/sched_tracker.h"

#include <stdbool.h>
#include <stdint.h>

/* Whether this member may HELLO a group's new cycle: it runs the program at the document's image (inv:
 * the driver's inventory after the cycle's actions) and the cycle's task, if any, ran here (task:
 * NULL not run; else *task_ok and its error). Otherwise false with the reason. */
bool mrc_cycle_ready(const mfd_group *g, uint64_t hash, const mfl_actual *inv, size_t ninv, const bool *task_ok, const char *task_err, char **why);

/* The stop order of programs moving together: a program that feeds another of them first. Returns a
 * permutation of names (new list). */
mt_strs mrc_stop_order(const mfd_doc *doc, const mt_strs *names);

typedef struct {
    void *ctx;
    uint32_t me;
    /* The driver (each true with *text, or false with *err): */
    mfl_actual *(*inventory)(void *ctx, size_t *n);
    /* An image of name at hash: the local catalog's when it is that image, else a member's. *src is
     * opaque to the reconciler, handed to load / upgrade and then to src_free. */
    bool (*source)(void *ctx, const char *name, const char *dir, uint64_t hash, void **src, char **err);
    void (*src_free)(void *ctx, void *src);
    bool (*load)(void *ctx, void *src, bool run, char **text, char **err);
    bool (*upgrade)(void *ctx, const char *name, void *src, char **text, char **err);
    bool (*unload)(void *ctx, const char *name, char **text, char **err);
    bool (*rollback)(void *ctx, const char *name, char **text, char **err);
    bool (*prog_run)(void *ctx, const char *name, bool on, char **text, char **err);
    bool (*reset)(void *ctx, const char *name, char **text, char **err);
    bool (*task)(void *ctx, const char *name, uint32_t op, uint32_t arg, uint32_t *result, char **err);
    bool (*place)(void *ctx, const char *name, const mt_strs *words, char **text, char **err);
    /* A migrated program's view from member from (the fleet thread asks it), and writing it here. */
    bool (*fetch_view)(void *ctx, const char *name, uint64_t hash, uint32_t from, uint32_t *view_abi, mpw_bytes *bytes, char **err);
    bool (*view_put)(void *ctx, const char *name, uint32_t view_abi, const mpw_bytes *bytes, char **text, char **err);
    /* The lockstep group shared with the fleet thread: stop feeding and wait until it saw it (mirror:
     * keep exchanging rounds); the group words in the mailbox; ready for an epoch or cycle (HELLO). */
    void (*quiesce)(void *ctx, bool mirror, bool member);
    bool (*want_set)(void *ctx);
    void (*group_words)(void *ctx, uint32_t rank, uint32_t n, uint32_t epoch);
    void (*ready)(void *ctx, const mpk_hello *spec);
    bool (*is_leader)(void *ctx);
    /* Publishing: the reconcile status (RS_*; reason at most BEAT_REASON_MAX) with this member's move
     * outcomes, and the programs not to try again (name, hash, unsupported, why <= 200 chars). */
    void (*status)(void *ctx, uint64_t applied, uint32_t status, const char *reason, const mst_outcome *moves, size_t nmoves);
    void (*fails)(void *ctx, char *const *names, const uint64_t *hashes, const bool *unsupported, char *const *whys, size_t n);
    uint64_t (*applied)(void *ctx);
    void (*log)(void *ctx, bool error, const char *line);
    void (*sleep_ms)(void *ctx, uint32_t ms);
} mrc_ops;

typedef struct mrc_rec mrc_rec;

mrc_rec *mrc_new(const mrc_ops *ops);
void mrc_free(mrc_rec *r);
/* A committed document (new: a version this reconciler did not see last): one pass. Returns true when
 * it failed (retry in 30 s with mrc_retry). */
bool mrc_document(mrc_rec *r, const mfd_doc *doc);
/* The retry: failed programs get another go on the last document. */
bool mrc_retry(mrc_rec *r);

/* A thread running a reconciler: post committed documents; it retries failures after 30 s. */
typedef struct mrc_thread mrc_thread;
mrc_thread *mrc_thread_start(const mrc_ops *ops);
void mrc_post(mrc_thread *t, const mfd_doc *doc);
void mrc_thread_stop(mrc_thread *t);

#endif
