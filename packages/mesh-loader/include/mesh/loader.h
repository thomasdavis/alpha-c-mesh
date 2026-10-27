/* @ajax/mesh-loader: everything about a load that happens outside the blink
 * (mesh-host loader.rs): resolving a catalog name under --programs DIR
 * (nothing else is readable), reading the manifest and PTX, an image's hash
 * and identity, and the catalog listing.  The backend's compiler then checks
 * and compiles on a worker thread. */
#ifndef MESH_LOADER_H
#define MESH_LOADER_H

#include "mesh/manifest.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A program's bytes as the catalog holds them (and F_LOAD carries them). */
typedef struct {
    char *dir;      /* the catalog dir name (under --programs) */
    char *manifest; /* program.toml's text */
    uint8_t *ptx;   /* may be NULL when ptx_len is 0 */
    size_t ptx_len;
} mesh_payload;

void mesh_payload_copy(mesh_payload *dst, const mesh_payload *src);
void mesh_payload_free(mesh_payload *p);

/* A catalog name: [a-z0-9-]{1,64}, optionally followed by "/program.toml" (and
 * trailing slashes).  The name (malloc'd), or NULL with *err:
 * "manifest: \"../etc\": programs are named [a-z0-9-] under --programs (no paths)". */
char *mesh_catalog_name(const char *spec, char **err);
/* The program dir for spec, which must stay inside root (symlinks too): the
 * canonical path (malloc'd), or NULL with *err. */
char *mesh_loader_resolve(const char *root, const char *spec, char **err);
/* The catalog dir spec's manifest text and PTX.  need_ptx: a missing PTX is
 * an error (CUDA compiles it); otherwise it is read when present. */
bool mesh_loader_read(const char *root, const char *spec, bool need_ptx, mesh_payload *out, char **err);
/* An image's identity: fnv1a64 of its code, with `param` folded in when not 0. */
uint64_t mesh_image_hash(const uint8_t *code, size_t n, uint32_t param);
/* A native program's identity: the manifest and its source.hash (trimmed), and param. */
uint64_t mesh_native_identity(const char *manifest, const char *source, uint32_t param);
/* A catalog dir's payload, manifest and image identity as a member's executor
 * computes it: the PTX's hash when the dir has one, else the first image the
 * manifest's backends lists that the dir holds (native source.hash, .wasm, MSL). */
bool mesh_loader_identity(const char *root, const char *spec, mesh_payload *p, mesh_manifest *m, uint64_t *hash, char **err);
/* A peer's F_LOAD hash must be the bytes' (expect NULL: nothing announced). */
bool mesh_check_hash(const char *name, const char *what, uint64_t hash, const uint64_t *expect, char **err);
/* The program dirs under root (those with a program.toml), sorted. */
bool mesh_catalog(const char *root, char ***names, size_t *n, char **err);

#endif
