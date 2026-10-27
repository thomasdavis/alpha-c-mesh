/* @ajax/mesh-ref-sha256d: programs/sha256d's CPU reference (mesh-ref sha.rs): SHA-256 (FIPS 180-4,
 * plainly), double SHA-256, a header hashed at a nonce and time, and Bitcoin's little-endian 256-bit
 * targets. Freestanding (no libc). */
#ifndef MESH_REF_SHA256D_H
#define MESH_REF_SHA256D_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_SHA_GENESIS_NONCE 2083236893u
#define MESH_SHA_GENESIS_HASH "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"

void mesh_sha256(const void *msg, size_t len, uint8_t out[32]);
void mesh_sha256d(const void *msg, size_t len, uint8_t out[32]);
/* The hash of hdr with `ntime` at bytes 68..72 and `nonce` at 76..80 (little-endian). */
void mesh_sha_hash_at(const uint8_t hdr[80], uint32_t nonce, uint32_t ntime, uint8_t out[32]);
/* Little-endian 256-bit a < b (byte 31 the most significant). */
bool mesh_sha_le_lt(const uint8_t a[32], const uint8_t b[32]);
/* Leading zero bits of a hash as a little-endian 256-bit number. */
uint32_t mesh_sha_zbits(const uint8_t h[32]);
/* The target whose top z bits are zero (every other bit set). */
void mesh_sha_zbits_target(uint32_t z, uint8_t out[32]);
/* Bytes in reverse order as lowercase hex (Bitcoin's display): out holds 65 bytes. */
void mesh_sha_display(const uint8_t h[32], char out[65]);
/* Bytes in order as lowercase hex: out holds 2n + 1 bytes. */
void mesh_sha_hex(const uint8_t *b, size_t n, char *out);

#endif
