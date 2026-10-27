/* @ajax/mesh-fleet-store: the member's fleet state file (`--fleet-state`): its term, vote and the
 * documents (mesh-fleet-persist), written whole to a temp file, synced, renamed into place and the
 * directory synced, before any vote, acknowledgement or commit leaves this member. */
#ifndef MESH_FLEET_STORE_H
#define MESH_FLEET_STORE_H

#include "mesh/fleet_persist.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char *path;
    uint32_t n;
} mfs_store;

/* Opens the file (absent: a fresh member) and writes it back at once (so the directory is writable
 * before the first vote). On error *err says which file and why. */
bool mfs_open(const char *path, uint32_t n, mfs_store *out, mfp_persist *p, char **err);
bool mfs_write(const mfs_store *s, const mfp_persist *p, char **err);
void mfs_close(mfs_store *s);

#endif
