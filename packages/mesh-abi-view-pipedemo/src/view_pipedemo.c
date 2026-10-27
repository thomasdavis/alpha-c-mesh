/* @ajax/mesh-abi-view-pipedemo: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/view_pipedemo.h"

void mesh_pipedemo_msg(uint32_t seq, uint64_t stamp, uint32_t origin, uint32_t out[16]) {
    out[MESH_PIPEDEMO_R_SEQ] = seq;
    out[MESH_PIPEDEMO_R_STAMP_LO] = (uint32_t)stamp;
    out[MESH_PIPEDEMO_R_STAMP_HI] = (uint32_t)(stamp >> 32);
    out[MESH_PIPEDEMO_R_ORIGIN] = origin;
    for (size_t i = MESH_PIPEDEMO_R_PAYLOAD; i < 16; i++) out[i] = mesh_pipedemo_payload(seq, i);
}

static uint32_t swap_bytes(uint32_t x) { return x >> 24 | (x >> 8 & 0xff00u) | (x << 8 & 0xff0000u) | x << 24; }

void mesh_pipedemo_sha256d_msg(const uint32_t m[16], uint32_t out[8]) {
    uint32_t h[8], w[16], pad[16] = {0}, w2[16] = {0};
    for (int i = 0; i < 8; i++) h[i] = SHA256_H0[i];
    for (int i = 0; i < 16; i++) w[i] = swap_bytes(m[i]);
    sha256_compress(h, w);
    pad[0] = 0x80000000u;
    pad[15] = 512;
    sha256_compress(h, pad);
    for (int j = 0; j < 8; j++) w2[j] = h[j];
    w2[8] = 0x80000000u;
    w2[15] = 256;
    for (int i = 0; i < 8; i++) out[i] = SHA256_H0[i];
    sha256_compress(out, w2);
}

void mesh_pipedemo_digest(const uint32_t m[16], uint32_t path, uint32_t out[12]) {
    uint32_t d[8];
    mesh_pipedemo_sha256d_msg(m, d);
    out[MESH_PIPEDEMO_R_SEQ] = m[MESH_PIPEDEMO_R_SEQ];
    out[MESH_PIPEDEMO_R_STAMP_LO] = m[MESH_PIPEDEMO_R_STAMP_LO];
    out[MESH_PIPEDEMO_R_STAMP_HI] = m[MESH_PIPEDEMO_R_STAMP_HI];
    out[MESH_PIPEDEMO_R_ORIGIN] = m[MESH_PIPEDEMO_R_ORIGIN] | path;
    for (int i = 0; i < 8; i++) out[MESH_PIPEDEMO_R_PAYLOAD + i] = d[i];
}
