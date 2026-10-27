/* @ajax/mesh-blobs-durable: a blob made durable under dir/name (blobs.rs `make_durable`,
 * `write_durable`, `sync_dir`, `fsync`): written to `name.tmp`, fsynced, renamed, so it is either
 * whole under its name or absent (a crash leaves at most a .tmp). The rename is durable once the
 * directory is fsynced, which the caller batches. A job's copy of bytes another job already holds is a
 * hard link of that file (the bytes are on disk once). fsync(2), not a drive cache flush (macOS's
 * F_FULLFSYNC): the job log's guarantee too. */
#ifndef MESH_BLOBS_DURABLE_H
#define MESH_BLOBS_DURABLE_H

#include <stdbool.h>
#include <stddef.h>

/* mkdir -p. False with *err. */
bool mb_mkdirs(const char *dir, char **err);
/* The bytes under dir/name, durably: written to name.tmp, fsynced, renamed. */
bool mb_write_durable(const char *dir, const char *name, const void *bytes, size_t n, char **err);
/* A hard link of `link` (another job's whole file of the same bytes) as dir/name when link is not
 * NULL, else (or if that file went meanwhile) the bytes written (mb_write_durable). */
bool mb_make_durable(const char *dir, const char *name, const char *link, const void *bytes, size_t n, char **err);
/* fsync(2) a directory (its entries: the renames into it). */
bool mb_sync_dir(const char *dir, char **err);
/* rm -rf of a directory tree (missing is fine). */
void mb_remove_tree(const char *dir);

#endif
