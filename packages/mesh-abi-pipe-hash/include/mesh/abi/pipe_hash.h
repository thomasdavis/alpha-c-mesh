/* @ajax/mesh-abi-pipe-hash: Pipe hashes: a record's position-mixed sum and an inbox type's FNV-1a 32.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PIPE_HASH_H
#define MESH_ABI_PIPE_HASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Word i's part of a record sum (the sum: these, wrapping, over every word). */
static inline uint32_t mesh_pipe_sum_part(uint32_t i, uint32_t w) {
    return (w ^ i * 0x9e3779b9u) * (2 * i + 1) + i;
}
/* A record's sum (its little-endian u32 words; a trailing partial word is ignored). */
static inline uint32_t mesh_pipe_rec_sum(const uint8_t *rec, size_t len) {
    uint32_t s = 0;
    for (size_t i = 0; 4 * i + 4 <= len; i++) {
        const uint8_t *b = rec + 4 * i;
        uint32_t w = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        s += mesh_pipe_sum_part((uint32_t)i, w);
    }
    return s;
}
/* Position p's part of its record's sum (murmur3's fmix32, a bijection: two
 * positions never mix alike). */
static inline uint32_t mesh_pipe_pos_mix(uint32_t p) {
    uint32_t x = p ^ 0x6d2b79f5u;
    x = (x ^ (x >> 16)) * 0x85ebca6bu;
    x = (x ^ (x >> 13)) * 0xc2b2ae35u;
    return x ^ (x >> 16);
}
/* The sum stored beside position p's record. */
static inline uint32_t mesh_pipe_rec_sum_at(const uint8_t *rec, size_t len, uint32_t p) {
    return mesh_pipe_rec_sum(rec, len) ^ mesh_pipe_pos_mix(p);
}
/* FNV-1a 32 of a type name (PP_IN_TYPE). */
static inline uint32_t mesh_pipe_type_hash(const uint8_t *b, size_t n) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 0x01000193u;
    }
    return h;
}

#endif
