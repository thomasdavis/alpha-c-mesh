/* @ajax/sha256-compress: One SHA-256 compression function and the initial state, freestanding C.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef SHA256_COMPRESS_H
#define SHA256_COMPRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One SHA-256 compression (FIPS 180-4 §6.2.2) of the 16 big-endian message
 * words w into the state h. */
void sha256_compress(uint32_t h[8], const uint32_t w[16]);

/* SHA-256's initial state, H(0). */
extern const uint32_t SHA256_H0[8];

#endif
