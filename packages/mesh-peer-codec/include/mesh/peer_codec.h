/* @ajax/mesh-peer-codec: the fleet's peer link frames (mesh-abi "peer frames v6", peer.rs), byte for
 * byte: F_JOIN (ids, version, size, the GPU's capabilities) and its checks, the election and commit
 * messages of mesh-fleet-core, F_BEAT (the core's beat plus this member's reconcile status and its move
 * outcomes), and F_HELLO (a lockstep group member ready for an epoch and cycle). Frames are the type
 * byte then the body. */
#ifndef MESH_PEER_CODEC_H
#define MESH_PEER_CODEC_H

#include "mesh/abi/base.h"
#include "mesh/fleet_core.h"
#include "mesh/place_wire.h"
#include "mesh/sched_tracker.h"

#include <stdbool.h>
#include <stdint.h>

/* This machine's capabilities, sent in F_JOIN. */
typedef struct {
    uint32_t cc, sms, sms_total, blocks_per_sm;
    char gpu[32], arch[16], host[32]; /* NUL terminated, as on the wire (at most 31 / 15 / 31 bytes) */
    uint32_t backend;                 /* H_BACKEND: 0 CUDA; Metal: BACKEND_METAL | family << 8 */
    uint32_t max_threads, max_shared; /* a node's limits on its GPU; 0: no GPU */
} mpk_caps;

typedef struct {
    uint32_t system, version, systems;
    mpk_caps caps;
} mpk_join;

mpw_bytes mpk_join_encode(const mpk_join *j);
/* b: JOIN_BYTES after the type byte. */
mpk_join mpk_join_decode(const uint8_t *b);
/* Whether a JOIN may open a link here: version, size, and (has_expect) the system dialed, or (not)
 * one of the systems that dial us. */
bool mpk_join_check(const mpk_join *j, uint32_t me, uint32_t n, bool has_expect, uint32_t expect, char **err);
bool mpk_caps_eq(const mpk_caps *a, const mpk_caps *b);

/* A member's status as its F_BEAT says (beside the fleet core's part). */
typedef struct {
    bool has_leader;
    uint32_t leader;
    uint64_t applied;
    uint32_t status, up, group_epoch, group_state;
    char *reason;
    mst_outcome *moves; /* what its reconciler did with programs moves placed on it */
    size_t nmoves;
} mpk_beat_status;

void mpk_beat_status_free(mpk_beat_status *s);
mpk_beat_status mpk_beat_status_clone(const mpk_beat_status *s);
bool mpk_beat_status_eq(const mpk_beat_status *a, const mpk_beat_status *b);

/* A core message as a frame (not a Beat: mpk_beat_frame). */
mpw_bytes mpk_msg_frame(const mfc_msg *m);
mpw_bytes mpk_beat_frame(const mfc_msg *beat, const mpk_beat_status *st);
/* h: BEAT_HEAD bytes; reason; moves: the k outcomes' bytes after the count. */
bool mpk_beat_decode(const uint8_t *h, const uint8_t *reason, size_t nreason, const uint8_t *moves, size_t nmoves, mfc_msg *msg, mpk_beat_status *st, char **err);
/* The fixed-size core messages from their bodies (after the type byte). */
mfc_msg mpk_vote_req_decode(const uint8_t *b);
mfc_msg mpk_vote_decode(const uint8_t *b);
mfc_msg mpk_ack_decode(uint8_t type, const uint8_t *b); /* F_STATE_ACK or F_COMMIT */

/* A lockstep group member ready for (epoch, cycle) of its group. */
typedef struct {
    char group[MESH_NAME_BYTES + 1];
    uint32_t epoch, cycle, mask;
    uint8_t order[MESH_MAX_SYSTEMS]; /* rank order: system ids, 0xff past the end */
    uint64_t hash;
} mpk_hello;

bool mpk_hello_rank(const mpk_hello *h, uint32_t k, uint32_t *rank);
uint32_t mpk_hello_n(const mpk_hello *h);
bool mpk_hello_eq(const mpk_hello *a, const mpk_hello *b);
mpw_bytes mpk_hello_frame(uint32_t me, const mpk_hello *h);
/* b: HELLO_BYTES after the type byte. */
bool mpk_hello_decode(const uint8_t *b, uint32_t *system, uint32_t *version, mpk_hello *out, char **err);

#endif
