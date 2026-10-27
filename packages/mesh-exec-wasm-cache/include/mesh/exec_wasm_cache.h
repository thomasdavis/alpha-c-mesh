/* @ajax/mesh-exec-wasm-cache: meshd's `--wasm-cache DIR` (mesh-host/src/wasm.rs `Rt::module`,
 * `evict`): a compiled module is kept as `<wasm fnv1a64>-<engine hash>.cwasm` (16 hex digits each),
 * written to a temp file and renamed, and the directory is kept to at most a cap of bytes by
 * deleting the oldest `.cwasm` files (by modification time). */
#ifndef MESH_EXEC_WASM_CACHE_H
#define MESH_EXEC_WASM_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The compiled-module cache's size cap (the Mac's disk is tight). */
#define MESH_WASM_CACHE_CAP (256ull << 20)

/* The path of the cached module for wasm under dir (malloc'd). */
char *mesh_wasm_cache_path(const char *dir, const uint8_t *wasm, size_t n, uint64_t engine_hash);
/* Creates the directory (and its parents). False with *err: "--wasm-cache DIR: <why>". */
bool mesh_wasm_cache_dir(const char *dir, char **err);
/* Writes bytes to path through path.tmp and a rename. False with *err. */
bool mesh_wasm_cache_store(const char *path, const uint8_t *bytes, size_t n, char **err);
/* Reads a cached file: true with *out (malloc'd) and *n. */
bool mesh_wasm_cache_load(const char *path, uint8_t **out, size_t *n);
/* Deletes the oldest .cwasm files of dir until they total at most cap bytes. Returns how many
 * were evicted (and calls on_evict for each path, if given). */
size_t mesh_wasm_cache_evict(const char *dir, uint64_t cap, void (*on_evict)(const char *path));

#endif
