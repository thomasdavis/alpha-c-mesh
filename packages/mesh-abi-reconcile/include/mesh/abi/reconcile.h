/* @ajax/mesh-abi-reconcile: A member's reconcile status (RS_*).
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_RECONCILE_H
#define MESH_ABI_RECONCILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A member's reconcile status (F_BEAT, the fleet records): none yet, in
 * sync with the committed document, working on it, or some action failed. */
#define MESH_RS_NONE 0u
#define MESH_RS_SYNC 1u
#define MESH_RS_PENDING 2u
#define MESH_RS_FAILED 3u
#define MESH_RS_NAMES_LEN 4
extern const char *const MESH_RS_NAMES[4];

#endif
