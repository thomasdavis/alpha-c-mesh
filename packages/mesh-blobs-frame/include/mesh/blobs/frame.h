/* @ajax/mesh-blobs-frame: the blob frames on the peer links (blobd.rs), each a type byte, a u32
 * length, then the body, which starts with the blob's sha256:
 * - F_BLOB_PUT: hash, u64 job, u32 len, the bytes (a push of job's copy);
 * - F_BLOB_ACK: hash, u32 ok, u64 job (the receiver's copy is durable, or was refused);
 * - F_BLOB_GET: hash, u64 job (a fetch; any job's copy serves);
 * - F_BLOB: hash, u32 found, the bytes when found (a fetch's answer). */
#ifndef MESH_BLOBS_FRAME_H
#define MESH_BLOBS_FRAME_H

#include "mesh/jobs/id.h"
#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mbf_put(msb *out, const mj_hash *h, mj_job_id job, const void *bytes, uint32_t n);
void mbf_ack(msb *out, const mj_hash *h, bool ok, mj_job_id job);
void mbf_get(msb *out, const mj_hash *h, mj_job_id job);
void mbf_blob(msb *out, const mj_hash *h, const void *bytes, size_t n, bool found);

/* A blob frame's body read: the fields its type has (bytes point into the body). */
typedef struct {
    mj_hash hash;
    mj_job_id job;   /* PUT, ACK, GET */
    uint32_t len;    /* PUT: the declared length */
    bool ok;         /* ACK: durable; BLOB: found */
    const uint8_t *bytes; /* PUT, BLOB */
    size_t nbytes;
} mbf_body;

/* False when the body is too short for its type (blobd drops such a frame). */
bool mbf_decode(uint8_t ty, const uint8_t *body, size_t n, mbf_body *out);

#endif
