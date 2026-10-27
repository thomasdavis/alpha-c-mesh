/* @ajax/mesh-abi-fault: P_FAULT codes.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_FAULT_H
#define MESH_ABI_FAULT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* P_FAULT codes (low byte), with detail in the high bits (e.g. the bad offset << 8). */
#define MESH_FAULT_VIEW_BOUNDS 1u
#define MESH_FAULT_PEER_VIEW 2u
#define MESH_FAULT_RESP_BOUNDS 3u
/* The backend reported an error for the slot's GPU work (Metal: a command
 * buffer that ended in error); detail: the backend's error code. */
#define MESH_FAULT_GPU 4u
/* wasm: the guest trapped (unreachable, a panic, an out-of-bounds access to
 * its own linear memory, a stack overflow); detail: wasmtime's trap code + 1
 * (0: not a wasm trap code). */
#define MESH_FAULT_TRAP 5u
/* wasm: the task ran past its manifest's `task_ms` (epoch deadline); the lane
 * interrupted it. */
#define MESH_FAULT_TIME 6u
/* A push into a slot this program does not feed (no connected `feeds`
 * entry for it), or a record of another size than the consumer's inbox;
 * detail: the slot. */
#define MESH_FAULT_PIPE 7u
/* A shared-memory access outside the program's `shared_bytes` (or
 * misaligned for its type): `ctx.shared(off, len)`; detail: the offset. */
#define MESH_FAULT_SHARED_BOUNDS 8u

#endif
