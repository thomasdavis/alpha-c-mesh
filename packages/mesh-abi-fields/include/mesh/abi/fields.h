/* @ajax/mesh-abi-fields: Region layout checks: (offset, bytes) fields, overlap and extent.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_FIELDS_H
#define MESH_ABI_FIELDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One field of a region: its offset and size in bytes. */
typedef struct {
    size_t off;
    size_t bytes;
} mesh_field;

/* No two (offset, bytes) ranges overlap. */
static inline bool mesh_disjoint(const mesh_field *f, size_t n) {
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (f[i].off < f[j].off + f[j].bytes && f[j].off < f[i].off + f[i].bytes) return false;
    return true;
}

/* The end of a region's last-ending field. */
static inline size_t mesh_fields_end(const mesh_field *f, size_t n) {
    size_t e = 0;
    for (size_t i = 0; i < n; i++)
        if (f[i].off + f[i].bytes > e) e = f[i].off + f[i].bytes;
    return e;
}

#endif
