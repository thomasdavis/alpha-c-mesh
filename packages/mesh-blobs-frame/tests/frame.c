/* The blob frames' bytes (as blobd.rs writes them) and their reading back. */
#include "mesh/blobs/frame.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/check.h"

#include <string.h>

int main(void) {
    mj_hash h;
    for (int i = 0; i < 32; i++) h.b[i] = (uint8_t)i;
    msb o = {0};
    mbf_put(&o, &h, 0x0102030405060708u, "abc", 3);
    CHECK(o.n == 1 + 4 + 47 && (uint8_t)o.p[0] == MESH_F_BLOB_PUT && (uint8_t)o.p[1] == 47);
    mbf_body b;
    CHECK(mbf_decode(MESH_F_BLOB_PUT, (const uint8_t *)o.p + 5, o.n - 5, &b));
    CHECK(mj_hash_eq(&b.hash, &h) && b.job == 0x0102030405060708u && b.len == 3 && b.nbytes == 3 && memcmp(b.bytes, "abc", 3) == 0);
    msb_clear(&o);
    mbf_ack(&o, &h, true, 9);
    CHECK(o.n == 49 && (uint8_t)o.p[1] == 44);
    CHECK(mbf_decode(MESH_F_BLOB_ACK, (const uint8_t *)o.p + 5, o.n - 5, &b) && b.ok && b.job == 9);
    msb_clear(&o);
    mbf_get(&o, &h, 9);
    CHECK(o.n == 45 && (uint8_t)o.p[1] == 40 && mbf_decode(MESH_F_BLOB_GET, (const uint8_t *)o.p + 5, o.n - 5, &b));
    msb_clear(&o);
    mbf_blob(&o, &h, "xy", 2, true);
    CHECK(o.n == 43 && (uint8_t)o.p[1] == 38 && mbf_decode(MESH_F_BLOB, (const uint8_t *)o.p + 5, o.n - 5, &b) && b.ok && b.nbytes == 2);
    msb_clear(&o);
    mbf_blob(&o, &h, NULL, 0, false);
    CHECK(o.n == 41 && mbf_decode(MESH_F_BLOB, (const uint8_t *)o.p + 5, o.n - 5, &b) && !b.ok);
    /* Short bodies are dropped. */
    CHECK(!mbf_decode(MESH_F_BLOB_PUT, (const uint8_t *)o.p, 31, &b) && !mbf_decode(MESH_F_BLOB_ACK, (const uint8_t *)o.p, 40, &b));
    msb_free(&o);
    return mesh_check_done();
}
