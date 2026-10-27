/* @ajax/mesh-abi-host-links: Peer link records (L_*) and the frame kinds they count.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_LINKS_H
#define MESH_ABI_HOST_LINKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/peer_frames.h"
#include "mesh/abi/fields.h"

/* One link record (L_*) per member k, at link(k): the bytes and frames of
 * this meshd's peer link to system k, summed over every link to it since
 * start (its own record stays zero). */
#define MESH_S_LINKS 0x700
#define MESH_LINK_BYTES 0x80
#define MESH_L_RX_BYTES 0x00 /* u64 frame bytes read (type byte included; TCP/IP headers not) */
#define MESH_L_TX_BYTES 0x08 /* u64 frame bytes written */
#define MESH_L_RX_FRAMES 0x10 /* u64 x NET_KINDS frames read, by frame_kind */
#define MESH_L_TX_FRAMES 0x30 /* u64 x NET_KINDS frames written */
#define MESH_L_LOCAL 0x50 /* 32 bytes: the link's local address (ip:port), NUL padded */
#define MESH_L_IFACE 0x70 /* 16 bytes: the interface that address is on */
/* Frame kinds counted per link: a lockstep group's frames (F_ROUND, F_HELLO),
 * placement (F_FRAME, F_INPUT, F_PLACE), the desired state (F_STATE,
 * F_STATE_ACK, F_COMMIT, F_CTL, F_CTL_REPLY, F_IMAGE_REQ, F_IMAGE, F_VIEW_REQ,
 * F_VIEW: "state"), control and liveness (F_JOIN, F_BEAT, F_VOTE_REQ, F_VOTE,
 * F_LOADREP, F_PERF); the jobs' frames and their blobs count as "state". */
#define MESH_NET_KINDS 4
#define MESH_KIND_ROUND 0
#define MESH_KIND_PLACE 1
#define MESH_KIND_LOAD 2
#define MESH_KIND_CTL 3
#define MESH_KIND_NAMES_LEN MESH_NET_KINDS
extern const char *const MESH_KIND_NAMES[MESH_NET_KINDS];
#define MESH_LINK_FIELDS_LEN 6
extern const mesh_field MESH_LINK_FIELDS[6];

/* The link record of member k. */
static inline size_t mesh_link(uint32_t k) { return MESH_S_LINKS + MESH_LINK_BYTES * (size_t)k; }
/* The kind (KIND_*) a frame type is counted under. */
static inline size_t mesh_frame_kind(uint8_t ty) {
    switch (ty) {
    case MESH_F_ROUND: case MESH_F_HELLO: return MESH_KIND_ROUND;
    case MESH_F_FRAME: case MESH_F_INPUT: case MESH_F_PLACE: return MESH_KIND_PLACE;
    case MESH_F_STATE: case MESH_F_STATE_ACK: case MESH_F_COMMIT: case MESH_F_CTL: case MESH_F_CTL_REPLY:
    case MESH_F_IMAGE_REQ: case MESH_F_IMAGE: case MESH_F_VIEW_REQ: case MESH_F_VIEW: return MESH_KIND_LOAD;
    default: return ty >= MESH_F_JOB_AVAIL && ty <= MESH_F_BLOB ? MESH_KIND_LOAD : MESH_KIND_CTL;
    }
}

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_S_LINKS % 8 == 0), "S_LINKS.is_multiple_of(8)");
_Static_assert((MESH_LINK_BYTES % 8 == 0), "LINK_BYTES.is_multiple_of(8)");
_Static_assert(MESH_L_RX_FRAMES + 8 * MESH_NET_KINDS == MESH_L_TX_FRAMES, "L_RX_FRAMES + 8 * NET_KINDS == L_TX_FRAMES");
_Static_assert(MESH_L_TX_FRAMES + 8 * MESH_NET_KINDS <= MESH_L_LOCAL, "L_TX_FRAMES + 8 * NET_KINDS <= L_LOCAL");

#endif
