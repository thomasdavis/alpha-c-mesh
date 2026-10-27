/* @ajax/sha256-stream: SHA-256 over a stream of bytes (init, update, final), on
 * @ajax/sha256-compress. Freestanding. */
#ifndef SHA256_STREAM_H
#define SHA256_STREAM_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} sha256_stream;
void sha256_stream_init(sha256_stream *s);
void sha256_stream_update(sha256_stream *s, const void *data, size_t n);
void sha256_stream_final(sha256_stream *s, uint8_t out[32]);
/* The digest as 64 lowercase hex digits and a NUL. */
void sha256_stream_hex(const uint8_t digest[32], char out[65]);
#endif
