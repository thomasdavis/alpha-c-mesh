/* @ajax/mesh-abi-host-members: Member records (M_*): each cluster member as this meshd sees it.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_MEMBERS_H
#define MESH_ABI_HOST_MEMBERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

#define MESH_S_MEMBERS 0x240
#define MESH_MEMBER_BYTES 0x80
/* Member record fields (offsets within it). */
#define MESH_M_STATE 0x00 /* u32 MS_* */
#define MESH_M_CC 0x04 /* u32 major * 10 + minor */
#define MESH_M_SMS 0x08 /* u32 SMs its meshd runs programs on */
#define MESH_M_SMS_TOTAL 0x0c /* u32 */
#define MESH_M_BLOCKS_PER_SM 0x10 /* u32 */
#define MESH_M_RTT_US 0x14 /* f32 exchange round trip to it, last second */
/* f32 mean wait per exchange round, last second: how long after this
 * system published round n the member's round n arrived (0 if it was first).
 * The slowest member is the one everyone else waits for. */
#define MESH_M_WAIT_US 0x18
#define MESH_M_ROUNDS 0x1c /* u32 its rounds received in the last second */
#define MESH_M_ROUND 0x20 /* u64 its round counter as last mirrored here */
#define MESH_M_GPU 0x28 /* 32 bytes NUL padded */
#define MESH_M_ARCH 0x48 /* 16 bytes */
#define MESH_M_HOST 0x58 /* 32 bytes (its hostname) */
/* u32 its H_BACKEND word (F_JOIN's eighth word; 0 = CUDA from a peer that predates it). */
#define MESH_M_BACKEND 0x78
#define MESH_MS_ABSENT 0u
#define MESH_MS_LINKED 1u /* TCP link up and JOINed */
#define MESH_MS_UP 2u /* linked and beating: a member of the fleet's view */
/* Up, but silent for over a third of the dead time (a link blip or a stalled
 * member; it goes absent at the dead time). */
#define MESH_MS_SUSPECT 3u
#define MESH_MEMBER_STATES_LEN 4
extern const char *const MESH_MEMBER_STATES[4];
#define MESH_MEMBER_FIELDS_LEN 13
extern const mesh_field MESH_MEMBER_FIELDS[13];

/* Member k's record. */
static inline size_t mesh_member(uint32_t k) { return MESH_S_MEMBERS + MESH_MEMBER_BYTES * (size_t)k; }

#endif
