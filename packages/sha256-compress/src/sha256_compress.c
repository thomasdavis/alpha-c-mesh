/* @ajax/sha256-compress: its tables and functions, and its compile-time layout checks. */
#include "sha256_compress.h"

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

const uint32_t SHA256_H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

static uint32_t rotr(uint32_t x, unsigned n) { return x >> n | x << (32 - n); }

void sha256_compress(uint32_t h[8], const uint32_t w[16]) {
    uint32_t x[64];
    for (int i = 0; i < 16; i++) x[i] = w[i];
    for (int i = 16; i < 64; i++) {
        uint32_t a = x[i - 15], b = x[i - 2];
        uint32_t s0 = rotr(a, 7) ^ rotr(a, 18) ^ (a >> 3);
        uint32_t s1 = rotr(b, 17) ^ rotr(b, 19) ^ (b >> 10);
        x[i] = x[i - 16] + s0 + x[i - 7] + s1;
    }
    uint32_t s[8];
    for (int j = 0; j < 8; j++) s[j] = h[j];
    for (int r = 0; r < 64; r++) {
        uint32_t s1 = rotr(s[4], 6) ^ rotr(s[4], 11) ^ rotr(s[4], 25);
        uint32_t ch = s[6] ^ (s[4] & (s[5] ^ s[6]));
        uint32_t t1 = s[7] + s1 + ch + K[r] + x[r];
        uint32_t s0 = rotr(s[0], 2) ^ rotr(s[0], 13) ^ rotr(s[0], 22);
        uint32_t maj = (s[0] & s[1]) | (s[2] & (s[0] | s[1]));
        uint32_t t2 = s0 + maj;
        s[7] = s[6], s[6] = s[5], s[5] = s[4], s[4] = s[3] + t1;
        s[3] = s[2], s[2] = s[1], s[1] = s[0], s[0] = t1 + t2;
    }
    for (int j = 0; j < 8; j++) h[j] += s[j];
}
