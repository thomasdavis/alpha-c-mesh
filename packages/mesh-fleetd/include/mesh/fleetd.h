/* @ajax/mesh-fleetd: meshd's fleet member, wired over the daemon's shared state (main.rs's fleet
 * thread, peer.rs `run` as the daemon sees it, reconcile.rs `run`, load.rs `perf_report`).
 *
 * The fleet area's @ajax/mesh-peer-relay runs the links, the leader, the policies and the lockstep
 * data plane; it reaches the daemon only through `mpr_host`. This package is that host:
 * - the status block, the mailbox and the lockstep feed are mesh_shared's (`host`, `mb`,
 *   `lock_feed`, `round_of`, `rtt`, `wait`);
 * - the reconciler (@ajax/mesh-reconcile's thread) acts through driver commands (`mesh_shared_ask`:
 *   Load, Upgrade, Unload, Rollback, ProgRun, Reset, Task, Place, ViewPut, Inventory), takes images
 *   from the local catalog when they are the committed ones, else from a member (`mpr_fetch_image`),
 *   and migrated views from their source (`mpr_fetch_view`);
 * - the catalog (`catalog`, `image`) is --programs, read with @ajax/mesh-loader;
 * - placement frames go to the driver (Cmd PeerPlace / PeerFrame / PeerInput), and the fleet's view
 *   (FleetView) when it changes;
 * - jobs and blobs go to @ajax/mesh-jobd (`mjd_*`), `job ...` requests as JobEvent::Ctl;
 * - the texts and the fleet view's records go where the control port reads them (`fleet_text`,
 *   `groups_text`, `sched_*`, `fleet_view`).
 * Two bridges carry what other threads send the fleet thread through mesh_shared: the `peer`
 * channel's changes and wakes (control port, driver, jobs thread) become `mpr_change` / `mpr_wake`,
 * and every member's `link_out` / `bulk_out` queue is written to its link with `mpr_send`.
 * In a fleet, the load sampler's F_PERF (`mesh_load_perf`) is built here, with the reconciler's
 * failures, the perf board's echoes and the link RTTs. */
#ifndef MESH_FLEETD_H
#define MESH_FLEETD_H

#include "mesh/cluster_conf.h"
#include "mesh/peer_codec.h"
#include "mesh/peer_relay.h"
#include "mesh/shared.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct mesh_fleetd mesh_fleetd;

/* Before anything runs: reads (and checks) the member's fleet state file (NULL: in memory) for system
 * topo->me; *skip_startup when it holds a committed document (--state / --autoload are for a fresh
 * fleet). topo is copied. NULL with *err when the file is unusable. */
mesh_fleetd *mesh_fleetd_open(mesh_shared *sh, const mcc_topology *topo, const char *state_path, bool *skip_startup, char **err);

/* This machine's F_JOIN capabilities from the mailbox header (H_CC, H_SMS, ...), its GPU's name,
 * the architecture and the hostname. */
mpk_caps mesh_fleetd_caps(mesh_shared *sh, const char *gpu, const char *arch);

/* Starts the bridges, the reconciler and (once the driver's startup loads ran: sh->ready) the fleet
 * thread with its links. Sets mesh_load_perf. A fleet state that cannot be written fail-stops. */
void mesh_fleetd_start(mesh_fleetd *f, const mpk_caps *caps);

/* Stops the fleet thread, the reconciler and the bridges (at exit, after the driver returned). */
void mesh_fleetd_stop(mesh_fleetd *f);
void mesh_fleetd_free(mesh_fleetd *f);

/* For tests and the control port: the relay once it started (NULL before). */
mpr_relay *mesh_fleetd_relay(mesh_fleetd *f);

#endif
