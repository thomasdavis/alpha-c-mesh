/* Buffering bytes into 64-byte blocks, the FIPS 180-4 padding, the digest. */
#include "sha256_stream.h"
#include "sha256_compress.h"

static void block(sha256_stream *s) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)s->block[4 * i] << 24 | (uint32_t)s->block[4 * i + 1] << 16 | (uint32_t)s->block[4 * i + 2] << 8 | s->block[4 * i + 3];
    sha256_compress(s->h, w);
    s->used = 0;
}

void sha256_stream_init(sha256_stream *s) {
    for (int i = 0; i < 8; i++) s->h[i] = SHA256_H0[i];
    s->bytes = 0;
    s->used = 0;
}

void sha256_stream_update(sha256_stream *s, const void *data, size_t n) {
    const uint8_t *p = data;
    s->bytes += n;
    while (n > 0) {
        size_t k = 64 - s->used < n ? 64 - s->used : n;
        for (size_t i = 0; i < k; i++) s->block[s->used + i] = p[i];
        s->used += k;
        p += k;
        n -= k;
        if (s->used == 64) block(s);
    }
}

void sha256_stream_final(sha256_stream *s, uint8_t out[32]) {
    uint64_t bits = s->bytes * 8;
    s->block[s->used++] = 0x80;
    if (s->used > 56) {
        while (s->used < 64) s->block[s->used++] = 0;
        block(s);
    }
    while (s->used < 56) s->block[s->used++] = 0;
    for (int i = 0; i < 8; i++) s->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
    block(s);
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 4; k++) out[4 * i + k] = (uint8_t)(s->h[i] >> (24 - 8 * k));
}

void sha256_stream_hex(const uint8_t digest[32], char out[65]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    out[64] = 0;
}
