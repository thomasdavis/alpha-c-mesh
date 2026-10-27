/* @ajax/mesh-abi-wasm: The wasm executor's calling contract: module, exports, windows and info codes.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_WASM_H
#define MESH_ABI_WASM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_WASM_MODULE "mesh"
#define MESH_WASM_EXPORT_MEMORY "memory"
#define MESH_WASM_EXPORT_INIT "mesh_init"
#define MESH_WASM_EXPORT_TASK "mesh_task"
#define MESH_WASM_EXPORT_RT_ABI "mesh_rt_abi"
/* Exported FTZ helpers: the float guard's allow-list. */
#define MESH_WASM_FTZ_PREFIX "mesh_ftz_"
/* Windows (handles): the slot's own view; the exchange page (lockstep
 * programs only: EX_BASE .. + EX_BYTES); another slot's view, resolved by
 * `peer` for the running task. */
#define MESH_WASM_W_VIEW 0u
#define MESH_WASM_W_EXCHANGE 1u
#define MESH_WASM_W_PEER 0x100u
/* `info` codes. */
#define MESH_WASM_I_SLOT 0u
#define MESH_WASM_I_NODE 1u
#define MESH_WASM_I_NODES 2u
#define MESH_WASM_I_TICKET 3u
/* H_PEER and H_SYSTEMS (read live, as mesh-rt does). */
#define MESH_WASM_I_SYSTEM 4u
#define MESH_WASM_I_SYSTEMS 5u
/* PX_PARAM (read live). */
#define MESH_WASM_I_PARAM 6u
#define MESH_WASM_I_SMS 7u
#define MESH_WASM_I_VIEW_BYTES 8u
/* P_REQ_LEN clamped to the window's req_max. */
#define MESH_WASM_I_REQ_LEN 9u
#define MESH_WASM_I_REQ_OFF 10u
#define MESH_WASM_I_RESP_OFF 11u
#define MESH_WASM_I_RESP_MAX 12u
#define MESH_WASM_I_INPUT_LEN 13u
/* 1: the program is the lockstep one (window W_EXCHANGE exists). */
#define MESH_WASM_I_LOCKSTEP 14u
#define MESH_WASM_I_READS_MASK 15u
/* H_GROUP_RANK, H_GROUP_N (the lockstep group: `ctx.group()`), read live.  (18 is free: it was the F3 partition.) */
#define MESH_WASM_I_GROUP_RANK 16u
#define MESH_WASM_I_GROUP_N 17u

/* The window of another slot's view, resolved by `peer`. */
static inline uint32_t mesh_wasm_w_peer(uint32_t slot) { return MESH_WASM_W_PEER + slot; }

#endif
