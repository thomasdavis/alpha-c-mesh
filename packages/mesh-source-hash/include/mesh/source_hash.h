/* @ajax/mesh-source-hash: a native program's source identity (build-programs.sh's source_hash): the
 * SHA-256 of every source file under some folders, each as "path\0content" in byte order of the
 * paths, its first 16 hex digits. Every member that built the same tree shares it. */
#ifndef MESH_SOURCE_HASH_H
#define MESH_SOURCE_HASH_H
#include <stdbool.h>
#include <stddef.h>
/* Hashes the files under root/dirs[i] (paths relative to root, as the script's `find` prints them;
 * a missing folder adds nothing) whose names end in .c or .h or are sexc.json or program.toml,
 * outside any .sexc folder. out: 16 hex digits and a NUL. False on a read error. */
bool mesh_source_hash(const char *root, const char *const *dirs, size_t ndirs, char out[17]);
#endif
