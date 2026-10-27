/* @ajax/mesh-jobd: meshd's jobs thread and blob thread (jobd.rs `run`, blobd.rs `run` and its writers)
 * over the daemon's shared state (@ajax/mesh-shared).
 *
 * - The jobs thread runs this meshd's job node (@ajax/mesh-jobs-node) -- its replica of the fleet's job
 *   table, the leader's dispatcher when it leads, and its side of the leases -- over the peer links
 *   (F_JOB_* frames, F_LEASE, F_RESULT), the driver (MESH_CMD_JOB_LEASE / JOB_CUT / JOB_PROGS,
 *   MESH_JEV_DONE) and the job log (@ajax/mesh-jobd-store, fsynced before an answer or a send that
 *   depends on it; a failure is a fail-stop). A single meshd runs it too: it is its own leader (term 1).
 *   Every 250 ms it publishes the host block's S_JOB_* fields, the jobs view's records
 *   (sh->jobs_view_recs) and blobd's news.
 * - The blob thread runs @ajax/mesh-blobd on sh->blobs, with MESH_BLOBD_WRITERS writer threads making
 *   the references durable in batches.
 *
 * Wiring (meshd): mjd_open after mesh_shared_new (it creates sh->blobs and sh->jobs_ext, and replays
 * the job log and indexes the blob store before anything runs), `mesh_control_render = mjd_render`,
 * mjd_start once the driver runs. The fleet thread calls mjd_set_view on every change of its view
 * (peer.rs), mjd_set_sched with the scheduler's model (peer_sched.rs), and mjd_route_frame for the
 * link frames of types F_JOB_AVAIL..F_RESULT and F_BLOB_PUT..F_BLOB. */
#ifndef MESH_JOBD_JOBD_H
#define MESH_JOBD_JOBD_H
#include "mesh/jobs/node.h"
#include "mesh/jobs/select.h"
#include "mesh/shared.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A table frame waits this long for room in a link's queue before the rest of the batch to that
 * member is dropped (a gap it reports, or a pull asked again). */
#define MESH_JOBD_TABLE_SEND_WAIT_MS 2000
/* The driver's job programs are asked, the view published and blobd told, this often. */
#define MESH_JOBD_PROGS_MS 250
#define MESH_JOBD_PUBLISH_MS 250

typedef struct {
    const char *jobs_state; /* --jobs-state: the job log (NULL: the table in memory) */
    const char *blobs;      /* --blobs: the blob store's directory (NULL: in memory) */
    uint64_t blobs_cap;     /* bytes */
    uint64_t dead_ms;       /* a member silent this long is dead (the fleet's tuning; MESH_PEER_DEAD_MS) */
} mjd_config;

typedef struct mjd mjd;

/* The job log replayed and the blob store indexed; sh->blobs and sh->jobs_ext made. NULL with *err
 * (meshd fails). */
mjd *mjd_open(mesh_shared *sh, const mjd_config *cfg, char **err);
/* The jobs thread, the blob thread and its writers. */
void mjd_start(mjd *d);
/* Stops and joins them (tests; meshd exits without). sh->blobs and sh->jobs_ext stay until mjd_free. */
void mjd_stop(mjd *d);
/* After mjd_stop (or without mjd_start): frees what mjd_open made, sh->blobs and sh->jobs_ext with it. */
void mjd_free(mjd *d);

/* The fleet thread: its view (term, leader, alive, linked). Ignored before mjd_open and when single. */
void mjd_set_view(mesh_shared *sh, mj_view v);
/* The fleet thread: the scheduler's model for the lease choice (takes cx; the jobs thread picks it up). */
void mjd_set_sched(mesh_shared *sh, mj_sched *cx);
/* A link frame (type byte, u32 length, body) from member from: a job frame to the jobs thread, a blob
 * frame to the blob thread. False if it is neither or the thread is gone (the frame is dropped). */
bool mjd_route_frame(mesh_shared *sh, uint32_t from, const uint8_t *frame, size_t n);
/* `render [<job>|latest] [status|ppm|bmp|raw]` (the words after `render`) through the blob thread:
 * the answer's bytes, or false with *err. The signature of @ajax/mesh-control's mesh_control_render. */
bool mjd_render(mesh_shared *sh, char **words, size_t n, uint8_t **out, size_t *len, char **err);

#endif
