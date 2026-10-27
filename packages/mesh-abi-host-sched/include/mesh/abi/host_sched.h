/* @ajax/mesh-abi-host-sched: The scheduler region: placement flags, cordons, the committed placement per slot.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_SCHED_H
#define MESH_ABI_HOST_SCHED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The scheduler (plan F4; 0x1680..0x1800): the committed placement as this */
/* member knows it, per program slot, and the scheduler's state.  Written by the */
/* fleet thread with the fleet region. */
#define MESH_S_SCHED 0x1680
#define MESH_S_SCHED_FLAGS 0x1680 /* u32 SF_* */
#define MESH_S_SCHED_MOVES 0x1684 /* u32 migrations this member saw committed as done (the leader's count) */
#define MESH_S_SCHED_GAP_US 0x1688 /* u32 the last migration's gap (stop on the source to running on the target), µs */
#define MESH_S_CORDON 0x168c /* u32 bit k: system k cordoned */
#define MESH_S_BUSY_MASK 0x1690 /* u32 bit k: the owner policy holds system k busy */
#define MESH_S_HEAP_RUN 0x1694 /* u32 the view heap's largest free run, pages (the driver's; a view that big still loads) */
/* Per program slot s: where the desired state places it (PS_*). */
#define MESH_S_PLACED 0x16a0
#define MESH_PS_MASK 0xffu /* bits 0..8: the members it is placed on */
#define MESH_PS_ALL 0x100u /* replicas "all": every member that can run it */
#define MESH_PS_PINNED 0x200u /* the operator's pin */
#define MESH_PS_MIGRATING 0x400u /* a migration in flight; bits 16..24: the source + 1 */
#define MESH_PS_KNOWN 0x800u /* the program is in the desired state (0: a local program, no fleet) */
#define MESH_PS_FROM_SHIFT 16u
#define MESH_SF_PAUSED 1u /* `sched pause` */
#define MESH_SF_AUTOREFORM 2u /* `sched autoreform on` */
#define MESH_SF_MIGRATING 4u /* a migration is in flight */
#define MESH_S_SCHED_END 0x1720

/* Slot s's placement word. */
static inline size_t mesh_placed(uint32_t s) { return MESH_S_PLACED + 4 * (size_t)s; }

#endif
