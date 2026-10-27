/* @ajax/mesh-abi-peer-frames: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/peer_frames.h"

const uint8_t MESH_FRAMES[32] = {
    MESH_F_HELLO, MESH_F_ROUND, MESH_F_BEAT, MESH_F_VOTE_REQ, MESH_F_VOTE, MESH_F_STATE, MESH_F_STATE_ACK, MESH_F_COMMIT, MESH_F_PLACE, MESH_F_FRAME, MESH_F_INPUT, MESH_F_JOIN, MESH_F_LOADREP, MESH_F_CTL, MESH_F_CTL_REPLY, MESH_F_IMAGE_REQ, MESH_F_IMAGE, MESH_F_PERF, MESH_F_VIEW_REQ, MESH_F_VIEW, MESH_F_JOB_AVAIL, MESH_F_JOB_STATE, MESH_F_JOB_ACK, MESH_F_JOB_PULL, MESH_F_JOB_HAVE, MESH_F_LEASE, MESH_F_LEASE_CUT, MESH_F_RESULT, MESH_F_BLOB_PUT, MESH_F_BLOB_ACK, MESH_F_BLOB_GET, MESH_F_BLOB,
};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_F_VIEW < MESH_F_JOB_AVAIL, "F_VIEW < F_JOB_AVAIL");
_Static_assert(MESH_F_RESULT == MESH_F_JOB_AVAIL + 7, "F_RESULT == F_JOB_AVAIL + 7");
_Static_assert(MESH_F_BLOB_PUT == MESH_F_RESULT + 1, "F_BLOB_PUT == F_RESULT + 1");
_Static_assert(MESH_F_BLOB == MESH_F_BLOB_PUT + 3, "F_BLOB == F_BLOB_PUT + 3");
