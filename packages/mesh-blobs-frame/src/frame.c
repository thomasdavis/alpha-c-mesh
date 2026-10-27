/* blobd.rs's blob frames: the bytes it writes (ack, push, get, answer) and how it reads them. */
#include "mesh/blobs/frame.h"
#include "mesh/abi/peer_frames.h"

#include <string.h>

static void u32le(msb *o, uint32_t v) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> 8 * i);
    msb_put(o, b, 4);
}

static void u64le(msb *o, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> 8 * i);
    msb_put(o, b, 8);
}

void mbf_put(msb *o, const mj_hash *h, mj_job_id job, const void *bytes, uint32_t n) {
    msb_putc(o, (char)MESH_F_BLOB_PUT);
    u32le(o, 44 + n);
    msb_put(o, h->b, 32);
    u64le(o, job);
    u32le(o, n);
    msb_put(o, bytes, n);
}

void mbf_ack(msb *o, const mj_hash *h, bool ok, mj_job_id job) {
    msb_putc(o, (char)MESH_F_BLOB_ACK);
    u32le(o, 44);
    msb_put(o, h->b, 32);
    u32le(o, ok ? 1 : 0);
    u64le(o, job);
}

void mbf_get(msb *o, const mj_hash *h, mj_job_id job) {
    msb_putc(o, (char)MESH_F_BLOB_GET);
    u32le(o, 40);
    msb_put(o, h->b, 32);
    u64le(o, job);
}

void mbf_blob(msb *o, const mj_hash *h, const void *bytes, size_t n, bool found) {
    msb_putc(o, (char)MESH_F_BLOB);
    u32le(o, (uint32_t)(36 + (found ? n : 0)));
    msb_put(o, h->b, 32);
    u32le(o, found ? 1 : 0);
    if (found) msb_put(o, bytes, n);
}

static bool u32_at(const uint8_t *b, size_t n, size_t o, uint32_t *v) {
    if (n < o + 4) return false;
    *v = (uint32_t)b[o] | (uint32_t)b[o + 1] << 8 | (uint32_t)b[o + 2] << 16 | (uint32_t)b[o + 3] << 24;
    return true;
}

static bool u64_at(const uint8_t *b, size_t n, size_t o, uint64_t *v) {
    if (n < o + 8) return false;
    uint64_t x = 0;
    for (int i = 7; i >= 0; i--) x = x << 8 | b[o + (size_t)i];
    *v = x;
    return true;
}

bool mbf_decode(uint8_t ty, const uint8_t *b, size_t n, mbf_body *out) {
    memset(out, 0, sizeof *out);
    if (n < 32) return false;
    memcpy(out->hash.b, b, 32);
    uint32_t v;
    switch (ty) {
    case MESH_F_BLOB_PUT:
        if (!u64_at(b, n, 32, &out->job) || !u32_at(b, n, 40, &out->len)) return false;
        out->bytes = b + 44;
        out->nbytes = n - 44;
        return true;
    case MESH_F_BLOB_ACK:
        if (!u32_at(b, n, 32, &v) || !u64_at(b, n, 36, &out->job)) return false;
        out->ok = v != 0;
        return true;
    case MESH_F_BLOB_GET: return true;
    case MESH_F_BLOB:
        out->ok = u32_at(b, n, 32, &v) && v != 0;
        if (out->ok) {
            out->bytes = b + 36;
            out->nbytes = n - 36;
        }
        return true;
    default: return false;
    }
}
