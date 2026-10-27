/* mesh-fleetd's state, shared by the host callbacks (host.c), the reconciler's ops (recon.c), the
 * bridges (fleetd.c) and the F_PERF hook (perf.c). */
#ifndef MESH_FLEETD_INT_H
#define MESH_FLEETD_INT_H

#include "mesh/fleetd.h"
#include "mesh/reconcile.h"
#include "mesh/sched_report.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* reconcile.rs's timeouts: a driver action, the retry, an image or view from a member, a placement. */
#define FD_APPLY_MS 60000
#define FD_FETCH_MS 35000
#define FD_PLACE_MS 20000

struct mesh_fleetd {
    mesh_shared *sh;
    mcc_topology topo;
    char *state_path;
    mpk_caps caps;
    mpr_host host;
    _Atomic(mpr_relay *) relay;
    _Atomic bool starting; /* the fleet thread has not finished mpr_start yet */
    _Atomic bool stop;
    mrc_thread *rec;
    pthread_t fleet_t, bridge_t, fwd_t[MESH_MAX_SYSTEMS];
    bool fleet_on, bridge_on, fwd_on[MESH_MAX_SYSTEMS];
    pthread_mutex_t mu;
    /* The reconciler's status as the beats carry it (shared.rs ReconStatus), and its failures
     * (recon_fail: the F_PERF's). */
    uint64_t applied;
    uint32_t status;
    char *reason;
    mst_outcome *moves;
    size_t nmoves;
    msr_fail *fails;
    size_t nfails;
    /* The lockstep group as the reconciler wants it (shared.rs GroupLocal's want, mirror, member). */
    bool want_set, mirror, member;
};

/* The relay, waiting while the fleet thread is still starting it (NULL: it is not running). */
mpr_relay *fd_relay(mesh_fleetd *f);

mpr_host fd_host(mesh_fleetd *f);
mrc_ops fd_recon_ops(mesh_fleetd *f);
void fd_perf_set(mesh_fleetd *f);

/* The answer to a bridged change: ctx is the waiter's mesh_reply (taken). */
void fd_reply_fn(void *ctx, bool ok, const char *text);

/* driver commands */
bool fd_ask(mesh_shared *sh, mesh_cmd c, int64_t within_ms, mesh_result *out, char **err);
mfl_actual *fd_inventory(mesh_shared *sh, int64_t within_ms, size_t *n);

#endif
