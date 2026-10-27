/* @ajax/mesh-samplers: meshd's two sampler threads.
 *
 * - load (load.rs `sampler`): every second, this machine's host sample and
 *   every loaded program's (mesh-load-host, mesh-load-prog) into the host
 *   block's load regions and the mailbox's LOAD_PAGE; in a fleet, the member
 *   summary to every member (F_LOADREP) and the scheduler's report (F_PERF,
 *   through the fleet's hook).
 * - net (net.rs `sampler`): every second, the interfaces that carry (or
 *   carried) a peer link, and the total, into the host block's NIC records.
 *
 * Both run until sh->quit; a host that cannot be read is said once per
 * distinct error, and the sample's age shows it to viewers. */
#ifndef MESH_SAMPLERS_H
#define MESH_SAMPLERS_H

#include "mesh/abi.h"
#include "mesh/load_host.h"
#include "mesh/shared.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mesh_load_sampler(mesh_shared *sh);
void mesh_net_sampler(mesh_shared *sh);

/* The fleet's scheduler report (F_PERF) built from a sample and broadcast
 * (load.rs perf_report and its frame); set by meshd in a fleet. NULL: none. */
extern void (*mesh_load_perf)(mesh_shared *sh, const mesh_host_sample *h, const uint8_t *cls, size_t ncls, const uint8_t (*recs)[MESH_PLOAD_BYTES],
                              size_t n);

#endif
