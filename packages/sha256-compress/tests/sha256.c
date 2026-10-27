/* sha256_compress: FIPS 180-4's one-block example, SHA-256("abc"), and the
 * two-block "abcdbcde...nopq" message. */
#include "sha256_compress.h"
#include "mesh/check.h"

static void pad_one_block(const char *s, uint32_t w[16]) {
    uint8_t b[64] = {0};
    size_t n = strlen(s);
    memcpy(b, s, n);
    b[n] = 0x80;
    uint64_t bits = (uint64_t)n * 8;
    for (int i = 0; i < 8; i++) b[63 - i] = (uint8_t)(bits >> (8 * i));
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
}

int main(void) {
    uint32_t h[8], w[16];
    memcpy(h, SHA256_H0, sizeof h);
    pad_one_block("abc", w);
    sha256_compress(h, w);
    const uint32_t abc[8] = {0xba7816bf, 0x8f01cfea, 0x414140de, 0x5dae2223, 0xb00361a3, 0x96177a9c, 0xb410ff61, 0xf20015ad};
    for (int i = 0; i < 8; i++) CHECK_U(h[i], abc[i]);
    /* "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq": 56 bytes, two blocks. */
    const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t b[128] = {0};
    memcpy(b, s, 56);
    b[56] = 0x80;
    b[126] = 0x01, b[127] = 0xc0; /* 448 bits */
    memcpy(h, SHA256_H0, sizeof h);
    for (int blk = 0; blk < 2; blk++) {
        for (int i = 0; i < 16; i++) {
            const uint8_t *p = b + 64 * blk + 4 * i;
            w[i] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
        }
        sha256_compress(h, w);
    }
    const uint32_t two[8] = {0x248d6a61, 0xd20638b8, 0xe5c02693, 0x0c3e6039, 0xa33ce459, 0x64ff2167, 0xf6ecedd4, 0x19db06c1};
    for (int i = 0; i < 8; i++) CHECK_U(h[i], two[i]);
    return mesh_check_done();
}
