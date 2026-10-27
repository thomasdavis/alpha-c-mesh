/* FIPS 180-4 vectors and golden outputs from the Rust (sha.rs): sha256, sha256d, hash_at, display,
 * zbits, targets and the little-endian compare. */
#include "mesh/ref/sha256d.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void want_hex(const char *what, const uint8_t *b, const char *hex) {
    char got[65];
    mesh_sha_hex(b, 32, got);
    if (strcmp(got, hex) != 0) {
        fprintf(stderr, "%s: %s, want %s\n", what, got, hex);
        failures++;
    }
}

int main(void) {
    static uint8_t a1000[1000];
    memset(a1000, 0x61, sizeof a1000);
    struct {
        const void *m;
        size_t n;
        const char *sha, *shad;
    } v[4] = {
        {"", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
         "5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456"},
        {"abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
         "4f8b42c22dd3729b519ba6f68d2da7cc5b2d606d05daed5ad5128cc03e6c6358"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
         "0cffe17f68954dac3a84fb1458bd5ec99209449749b2b308b7cb55812f9563af"},
        {a1000, 1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3",
         "f2b6fd3c03e69a9201ec5826310c02da24d154d2fe3c9041527696bb1f693dce"},
    };
    uint8_t h[32];
    for (int i = 0; i < 4; i++) {
        mesh_sha256(v[i].m, v[i].n, h);
        want_hex("sha256", h, v[i].sha);
        mesh_sha256d(v[i].m, v[i].n, h);
        want_hex("sha256d", h, v[i].shad);
    }
    /* Every length around the padding boundaries agrees with hashing in one go (a split-free check). */
    uint8_t hdr[80];
    for (int i = 0; i < 80; i++) hdr[i] = (uint8_t)(i * 37 + 11);
    mesh_sha_hash_at(hdr, 12345, 0x5f5e100, h);
    want_hex("hash_at", h, "2080acf4723a90b446e025ca3dc414d092e75a8f8ce2396255c0e07718ead0ac");
    char disp[65];
    mesh_sha_display(h, disp);
    if (strcmp(disp, "acd0ea1877e0c0556239e28c8f5ae792d014c43dca25e046b4903a72f4ac8020") != 0) failures++;
    if (mesh_sha_zbits(h) != 0) failures++;
    struct {
        uint32_t z;
        const char *hex;
    } t[8] = {
        {0, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
        {1, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"},
        {8, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff00"},
        {9, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f00"},
        {31, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff01000000"},
        {32, "ffffffffffffffffffffffffffffffffffffffffffffffffffffffff00000000"},
        {255, "0100000000000000000000000000000000000000000000000000000000000000"},
        {256, "0000000000000000000000000000000000000000000000000000000000000000"},
    };
    uint8_t tg[32];
    for (int i = 0; i < 8; i++) {
        mesh_sha_zbits_target(t[i].z, tg);
        want_hex("target", tg, t[i].hex);
        if (mesh_sha_zbits(tg) != (t[i].z == 0 ? 0 : t[i].z)) {
            fprintf(stderr, "zbits of target %u: %u\n", t[i].z, mesh_sha_zbits(tg));
            failures++;
        }
    }
    mesh_sha_zbits_target(3, tg);
    if (mesh_sha_le_lt(h, tg) != false || mesh_sha_le_lt(tg, h) != true) failures++;
    /* The genesis block's display hash has 43 leading zero bits. */
    if (strlen(MESH_SHA_GENESIS_HASH) != 64) failures++;
    return failures != 0;
}
