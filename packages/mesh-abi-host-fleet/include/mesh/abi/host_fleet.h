/* @ajax/mesh-abi-host-fleet: The fleet region: leader, term, versions, members' reconcile status, the lockstep group.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_FLEET_H
#define MESH_ABI_HOST_FLEET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* The fleet (plan F3; 0x1400..0x1800): this member's view of the leader, the */
/* desired state and every member's reconcile status (from their F_BEATs), and */
/* its lockstep group.  Written by the fleet thread every tick. */
#define MESH_S_FLEET 0x1400
#define MESH_S_LEADER 0x1400 /* u32 the leader's system id, PEER_NONE: none reachable (no majority) */
#define MESH_S_ROLE 0x1404 /* u32 ROLE_* */
#define MESH_S_TERM 0x1408 /* u64 */
#define MESH_S_STATE_VER 0x1410 /* u64 the committed desired state's version (as this member knows it) */
#define MESH_S_APPLIED_VER 0x1418 /* u64 the version this member reconciled to */
#define MESH_S_UP_MASK 0x1420 /* u32 bit k: system k is up (linked and heard within the dead time), this one included */
#define MESH_S_SUSPECT_MASK 0x1424 /* u32 bit k: up but silent for a third of the dead time */
#define MESH_S_QUORUM 0x1428 /* u32 votes a majority needs (1: the fixed pair) */
#define MESH_S_FLEET_FLAGS 0x142c /* u32 FF_* */
/* 0x1430..0x1438: free (the F3 partition's words; F5's job chunks replaced the partition) */
#define MESH_S_DEAD_MS 0x1438 /* u32 the dead time in use */
#define MESH_S_COMMIT_TERM 0x143c /* u32 the committed document's term (low 32 bits) */
#define MESH_ROLE_SINGLE 0u
#define MESH_ROLE_FOLLOWER 1u
#define MESH_ROLE_PRECANDIDATE 2u
#define MESH_ROLE_CANDIDATE 3u
#define MESH_ROLE_LEADER 4u
#define MESH_ROLE_NAMES_LEN 5
extern const char *const MESH_ROLE_NAMES[5];
#define MESH_FF_QUORUM 1u /* this member hears a majority */
#define MESH_FF_FIXED 2u /* a pair: system 0 coordinates, no failover */
/* Per member k at fmember(k), FMEMBER_BYTES: its reconcile status as its last F_BEAT said. */
#define MESH_S_FMEMBERS 0x1440
#define MESH_FMEMBER_BYTES 0x40
#define MESH_FM_STATUS 0x00 /* u32 RS_* */
#define MESH_FM_GROUP 0x04 /* u32 GL_* of its lockstep group */
#define MESH_FM_APPLIED 0x08 /* u64 the version it reconciled to */
#define MESH_FM_HEARD_MS 0x10 /* u64 this meshd's uptime ms when it was last heard (0: never) */
#define MESH_FM_REASON 0x18 /* 40 bytes NUL padded: what failed / is pending */
/* This member's lockstep group (the one the document gives the loaded lockstep program). */
#define MESH_S_GROUP 0x1640
#define MESH_SG_NAME 0x1640 /* 16 bytes */
#define MESH_SG_EPOCH 0x1650 /* u32 */
#define MESH_SG_CYCLE 0x1654 /* u32 */
#define MESH_SG_MASK 0x1658 /* u32 the epoch's members */
#define MESH_SG_ORDER 0x165c /* 8 bytes: rank r's system id (0xff past the end) */
#define MESH_SG_STATE 0x1664 /* u32 the document's: 1 running, 2 paused (0: no group) */
#define MESH_SG_LOCAL 0x1668 /* u32 GL_*: how this member's data plane runs it */
#define MESH_SG_RANK 0x166c /* u32 this member's rank (PEER_NONE: not in the epoch) */
#define MESH_SG_ON_LOSS 0x1670 /* u32 0 pause, 1 reform */
#define MESH_SG_AUTO_MS 0x1674 /* u32 */
#define MESH_S_FLEET_END 0x1680
#define MESH_GS_NAMES_LEN 3
extern const char *const MESH_GS_NAMES[3];
#define MESH_FMEMBER_FIELDS_LEN 5
extern const mesh_field MESH_FMEMBER_FIELDS[5];

/* Member k's fleet record. */
static inline size_t mesh_fmember(uint32_t k) { return MESH_S_FMEMBERS + MESH_FMEMBER_BYTES * (size_t)k; }

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_SG_AUTO_MS + 4 <= MESH_S_FLEET_END, "SG_AUTO_MS + 4 <= S_FLEET_END");
_Static_assert((MESH_S_TERM % 8 == 0), "S_TERM.is_multiple_of(8)");
_Static_assert((MESH_S_STATE_VER % 8 == 0), "S_STATE_VER.is_multiple_of(8)");
_Static_assert((MESH_S_APPLIED_VER % 8 == 0), "S_APPLIED_VER.is_multiple_of(8)");
_Static_assert((MESH_FM_APPLIED % 8 == 0), "FM_APPLIED.is_multiple_of(8)");
_Static_assert((MESH_FMEMBER_BYTES % 8 == 0), "FMEMBER_BYTES.is_multiple_of(8)");
_Static_assert((MESH_S_FMEMBERS % 8 == 0), "S_FMEMBERS.is_multiple_of(8)");

#endif
