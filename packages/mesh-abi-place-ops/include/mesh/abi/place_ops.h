/* @ajax/mesh-abi-place-ops: F_PLACE ops: pause, paused, set, done, fail.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PLACE_OPS_H
#define MESH_ABI_PLACE_OPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* F_PLACE ops.  The leader runs a change among the up members: PL_PAUSE to
 * each -> (each drains) PL_PAUSED with its state -> PL_SET to each with the mode,
 * the member mask and the state that continues -> PL_DONE from each.  PL_FAIL
 * (the leader, any time): set the mode it names (the old one) and resume,
 * without copying state.  PL_REQ / PL_RESULT are unused since F3 (changes go
 * through the desired state) but keep their numbers. */
#define MESH_PL_REQ 0u
#define MESH_PL_RESULT 1u
#define MESH_PL_PAUSE 2u
#define MESH_PL_PAUSED 3u
#define MESH_PL_SET 4u
#define MESH_PL_DONE 5u
#define MESH_PL_FAIL 6u

#endif
