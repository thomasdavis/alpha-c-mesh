/* @ajax/mesh-abi-name: Valid program names: [a-z0-9-]{1,15}.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_NAME_H
#define MESH_ABI_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"

/* A valid program name: [a-z0-9-]{1,15}. */
static inline bool mesh_valid_name(const uint8_t *b, size_t n) {
    if (n == 0 || n > MESH_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

#endif
