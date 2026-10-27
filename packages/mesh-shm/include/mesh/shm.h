/* @ajax/mesh-shm: the mailbox's memory on every backend (mesh-host shm.rs):
 * one shared-memory file of MAILBOX_BYTES, mapped MAP_SHARED, page aligned
 * and zero filled, that other processes map too -- native workers, and the
 * --isolation mps workers.  The GPU backends wrap the same pages (CUDA
 * registers them with cuMemHostRegister DEVICEMAP).
 * - Linux: a memfd (close-on-exec); a child gets it as an inherited
 *   descriptor, an MPS worker opens /proc/<pid>/fd/<fd>.
 * - macOS: shm_open of a fresh name, unlinked at once.
 * The mapping lives as long as the process (never unmapped). */
#ifndef MESH_SHM_H
#define MESH_SHM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *base;
    size_t len;
    int fd; /* close-on-exec: a child gets it only through dup2 */
} mesh_shm;

/* A zeroed MAILBOX_BYTES mapping. False with *err ("mailbox: memfd_create: ..."). */
bool mesh_shm_new(mesh_shm *out, char **err);
bool mesh_shm_with_len(size_t len, mesh_shm *out, char **err);
/* A path another process can open it by (Linux: /proc/<pid>/fd/<fd>), malloc'd. */
char *mesh_shm_path(const mesh_shm *s);
/* Unmaps and closes (tests only: meshd never does). */
void mesh_shm_close(mesh_shm *s);

#endif
