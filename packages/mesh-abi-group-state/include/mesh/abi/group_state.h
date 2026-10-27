/* @ajax/mesh-abi-group-state: A member's lockstep group state (GL_*).
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_GROUP_STATE_H
#define MESH_ABI_GROUP_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A member's lockstep group, as its data plane runs it: not in one, paused
 * (a member lost, or the document says paused), HELLOing an epoch/cycle, running. */
#define MESH_GL_NONE 0u
#define MESH_GL_PAUSED 1u
#define MESH_GL_HELLO 2u
#define MESH_GL_RUNNING 3u
#define MESH_GL_NAMES_LEN 4
extern const char *const MESH_GL_NAMES[4];

#endif
