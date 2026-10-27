/* @ajax/mesh-state-file: meshd's --state file (mesh-host loader.rs Saved,
 * read_state, write_state): one line per loaded program, in slot order:
 * `name dir 0|1` (its catalog dir under --programs, and whether it runs).
 * Written whole and renamed into place, so a crash leaves the old file. */
#ifndef MESH_STATE_FILE_H
#define MESH_STATE_FILE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *name;
    char *dir;
    bool running;
} mesh_saved;

/* A missing file is an empty set.  False with *err, e.g.
 * "--state /x line 1: expected `name dir 0|1`, got \"hello ../x 1\"". */
bool mesh_state_read(const char *path, mesh_saved **set, size_t *n, char **err);
bool mesh_state_write(const char *path, const mesh_saved *set, size_t n, char **err);
void mesh_state_free(mesh_saved *set, size_t n);

#endif
