/* @ajax/mesh-abi-host-jobs: The jobs region: this member's replica of the fleet's job table.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_JOBS_H
#define MESH_ABI_HOST_JOBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Jobs (plan F5; 0x1740..0x1780, after F4's scheduler region 0x1680..0x1720): this member's replica of the fleet's job */
/* table and, on the leader, the dispatcher.  Written by the jobs thread */
/* (mesh-host jobd.rs) every 250 ms; a single meshd runs jobs too (its own leader). */
#define MESH_S_JOBS 0x1740
#define MESH_S_JOBS_ACTIVE 0x1740 /* u32 jobs submitted and not finished (committed) */
#define MESH_S_JOBS_TOTAL 0x1744 /* u32 jobs in the table (finished ones included) */
#define MESH_S_JOB_DONE 0x1748 /* u64 items done, every job (committed) */
#define MESH_S_JOB_FAILED 0x1750 /* u64 items failed for good (committed) */
#define MESH_S_JOB_LEASED 0x1758 /* u32 the leader: items in leases; another member: items it holds */
#define MESH_S_JOB_RATE 0x175c /* f32 items/s over the last second (the leader: the fleet's; else this member's) */
#define MESH_S_JOB_DUPES 0x1760 /* u64 duplicate results dropped (committed) */
#define MESH_S_JOB_COMMIT 0x1768 /* u64 the job table's commit point (entries of the leader's term) */
#define MESH_S_JOB_TERM 0x1770 /* u64 the term the table follows */
#define MESH_S_JOB_FLAGS 0x1778 /* u32 JF_* */
#define MESH_S_JOB_HELD 0x177c /* u32 leases this member runs */
#define MESH_S_JOBS_END 0x1780
#define MESH_JF_LEADER 1u /* this member leads the job table */
#define MESH_JF_ACTIVE 2u /* the leader merged a majority's tables and dispatches */
#define MESH_JF_PERSIST 4u /* the table is persisted (--jobs-state, or the fleet state file's .jobs) */

#endif
