/* @ajax/mesh-abi-fnv: FNV-1a 64, the hash of program images in the mesh contract.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_FNV_H
#define MESH_ABI_FNV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* FNV-1a 64: the PTX hash in the table, `programs` and F_HELLO / F_LOAD. */
static inline uint64_t mesh_fnv1a64(const uint8_t *b, size_t n) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= UINT64_C(0x100000001b3);
    }
    return h;
}

#endif
