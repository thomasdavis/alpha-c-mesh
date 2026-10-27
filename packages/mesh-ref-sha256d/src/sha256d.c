/* SHA-256 and the sha256d helpers, plainly (mesh-ref sha.rs). */
#include "mesh/ref/sha256d.h"

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void block(uint32_t h[8], const uint8_t *blk) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)blk[4 * i] << 24 | (uint32_t)blk[4 * i + 1] << 16 | (uint32_t)blk[4 * i + 2] << 8 |
               blk[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
}

void mesh_sha256(const void *msg, size_t len, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    const uint8_t *m = msg;
    size_t full = len / 64 * 64;
    for (size_t o = 0; o < full; o += 64) block(h, m + o);
    /* The padded tail: the rest, 0x80, zeros to 56 mod 64, the bit length big-endian. */
    uint8_t tail[128] = {0};
    size_t rest = len - full;
    for (size_t i = 0; i < rest; i++) tail[i] = m[full + i];
    tail[rest] = 0x80;
    size_t tlen = rest + 1 <= 56 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) tail[tlen - 1 - i] = (uint8_t)(bits >> (8 * i));
    for (size_t o = 0; o < tlen; o += 64) block(h, tail + o);
    for (int i = 0; i < 8; i++) {
        out[4 * i] = (uint8_t)(h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(h[i] >> 8);
        out[4 * i + 3] = (uint8_t)h[i];
    }
}

void mesh_sha256d(const void *msg, size_t len, uint8_t out[32]) {
    uint8_t once[32];
    mesh_sha256(msg, len, once);
    mesh_sha256(once, 32, out);
}

void mesh_sha_hash_at(const uint8_t hdr[80], uint32_t nonce, uint32_t ntime, uint8_t out[32]) {
    uint8_t h[80];
    for (int i = 0; i < 80; i++) h[i] = hdr[i];
    for (int i = 0; i < 4; i++) {
        h[68 + i] = (uint8_t)(ntime >> (8 * i));
        h[76 + i] = (uint8_t)(nonce >> (8 * i));
    }
    mesh_sha256d(h, 80, out);
}

bool mesh_sha_le_lt(const uint8_t a[32], const uint8_t b[32]) {
    for (int i = 31; i >= 0; i--)
        if (a[i] != b[i]) return a[i] < b[i];
    return false;
}

uint32_t mesh_sha_zbits(const uint8_t h[32]) {
    uint32_t z = 0;
    for (int i = 31; i >= 0; i--) {
        if (h[i] != 0) {
            uint32_t lz = 0;
            for (uint8_t b = h[i]; !(b & 0x80); b = (uint8_t)(b << 1)) lz++;
            return z + lz;
        }
        z += 8;
    }
    return z;
}

void mesh_sha_zbits_target(uint32_t z, uint8_t out[32]) {
    for (int i = 0; i < 32; i++) out[i] = 0xff;
    if (z > 256) z = 256;
    for (uint32_t bit = 256 - z; bit < 256; bit++) out[bit / 8] &= (uint8_t)~(1u << (bit % 8));
}

static const char HEX[] = "0123456789abcdef";

void mesh_sha_display(const uint8_t h[32], char out[65]) {
    for (int i = 0; i < 32; i++) {
        out[2 * i] = HEX[h[31 - i] >> 4];
        out[2 * i + 1] = HEX[h[31 - i] & 15];
    }
    out[64] = 0;
}

void mesh_sha_hex(const uint8_t *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = HEX[b[i] >> 4];
        out[2 * i + 1] = HEX[b[i] & 15];
    }
    out[2 * n] = 0;
}
