/* @ajax/mesh-abi-peer-sizes: Peer frame body sizes and limits.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PEER_SIZES_H
#define MESH_ABI_PEER_SIZES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"

#define MESH_F_PERF_MAX (16 << 10)
#define MESH_VIEW_REQ_BYTES (4 + MESH_NAME_BYTES + 8)
#define MESH_VIEW_HEAD (4 * 6)
#define MESH_F_VIEW_CHUNK (1 << 20)
/* A blob frame's body, at most (a blob is at most JOB_PAYLOAD_MAX). */
#define MESH_F_BLOB_MAX (MESH_JOB_PAYLOAD_MAX  + 64)
/* A job item's payload (`job_payload`), at most. */
#define MESH_JOB_PAYLOAD_MAX ((uint32_t)(1 << 20))
/* A job frame's head (after the type byte): u32 len. */
#define MESH_JOB_HEAD 4
#define MESH_F_JOB_MAX (8 << 20)
#define MESH_JOIN_BYTES (8 * 4 + 32 + 16 + 32 + 2 * 4)
#define MESH_HELLO_BYTES (4 + 4 + MESH_NAME_BYTES + 4 + 4 + 4 + MESH_MAX_SYSTEMS  + 8)
#define MESH_ROUND_HEAD (4 + 4 + 8)
#define MESH_BEAT_HEAD (8 + 4 + 4 + 8 * 5 + 4 * 5)
#define MESH_BEAT_REASON_MAX 160
/* F_BEAT's per-program move outcomes: at most this many, each this long. */
#define MESH_BEAT_MOVES_MAX (MESH_PROG_SLOTS)
#define MESH_BEAT_MOVE_BYTES (MESH_NAME_BYTES + 8 + 4 + 4)
#define MESH_BEAT_LEADER 1u
#define MESH_VOTE_REQ_BYTES (4 + 8 + 8 + 8)
#define MESH_VOTE_BYTES (4 + 8 + 4)
#define MESH_STATE_HEAD (8 + 4)
#define MESH_F_STATE_MAX (1 << 20)
#define MESH_ACK_BYTES (8 + 8 + 8)
#define MESH_CTL_HEAD (4 + 4)
#define MESH_F_CTL_MAX 4096
#define MESH_CTL_REPLY_HEAD (4 + 4 + 4)
#define MESH_F_CTL_REPLY_MAX (64 << 10)
#define MESH_IMAGE_REQ_HEAD (4 + MESH_NAME_BYTES + 8 + 4)
#define MESH_IMAGE_HEAD (4 + 8 + 4 + 4 + 4 + 4)
#define MESH_F_PLACE_MAX (64 << 10)
#define MESH_F_INPUT_MAX 256
#define MESH_F_LOAD_MAX (4 << 20)
#define MESH_F_REASON_MAX 256

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_JOB_HEAD == 4, "JOB_HEAD == 4");
_Static_assert(MESH_F_BLOB_MAX <= MESH_F_JOB_MAX, "F_BLOB_MAX <= F_JOB_MAX");

#endif
