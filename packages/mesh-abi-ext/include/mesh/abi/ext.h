/* @ajax/mesh-abi-ext: The manifest extension (PX_*): counter names, fields, placement, occupancy, param, executor, shared and scratch.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_EXT_H
#define MESH_ABI_EXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"
#include "mesh/abi/control_block.h"
#include "mesh/abi/placement.h"
#include "mesh/abi/fields.h"

/* ---------------------------------------------------------------- manifest extension */
/* Per slot, PX_BYTES at `prog_ext(s)`: the manifest's display metadata, for
 * viewers and the http program (meshd writes it with the table entry). */
#define MESH_PROG_EXT 0x30000
#define MESH_PX_BYTES 0x400
#define MESH_PX_COUNTER_NAMES 0x00 /* [u8; 16] x COUNTERS, NUL padded */
#define MESH_PX_FIELD_COUNT 0x40 /* u32 */
/* u32 PLACE_CAP_* (the manifest's `place`), written with the extension. */
#define MESH_PX_PLACE_CAP 0x44
/* u32 PLACE_* and u32 the system it names (pin: the runner; split: the
 * owner), written by meshd at every placement change (and after any rewrite
 * of the extension).  0/0 = local. */
#define MESH_PX_PLACE 0x48
#define MESH_PX_PLACE_SYS 0x4c
/* [u8; PLACE_PARTS_MAX]: split's assignment, part b -> system (PART_NONE: no part / not split). */
#define MESH_PX_PLACE_ASSIGN 0x50
/* u32 cuOccupancyMaxActiveBlocksPerMultiprocessor of the image's mesh_main
 * (one-warp blocks) on this machine's GPU, and u32 its local bytes per
 * thread: written by meshd with the extension (per machine: PTX is shared,
 * SASS and occupancy are not). */
#define MESH_PX_PER_SM 0x70
#define MESH_PX_LOCAL 0x74
/* u32 the manifest's `param` (default 0), written with the extension: a
 * program constant every member of a cluster reads alike (it is folded into
 * the image hash, which replication and the lockstep HELLO compare).
 * sha256d: log2 of the nonces per task, so g = N c + k tiles the nonce space. */
#define MESH_PX_PARAM 0x78
/* u32, host: the executor running the slot's current image, BACKEND_* (cuda,
 * metal: the machine's GPU; wasm: lease lanes on the CPU; native: a worker
 * process).  Set at load, upgrade and rollback with the rest of the entry. */
#define MESH_PX_EXEC 0x7c
#define MESH_PX_FIELDS 0x80 /* FIELD_BYTES x MAX_FIELDS */
#define MESH_FIELD_BYTES 32
#define MESH_MAX_FIELDS 27
/* u32 the manifest's `shared_bytes` (the program's shared memory per node,
 * after the runtime's RT_SHARED_BYTES), written with the extension. */
#define MESH_PX_SHARED (MESH_PX_FIELDS + MESH_FIELD_BYTES * MESH_MAX_FIELDS)
/* u32 the register cap the image was compiled with on this machine (CUDA:
 * nvJitLink -maxrregcount, so that a block of 32 x warps threads fits the
 * SM's register file; 0: none), written with the extension. */
#define MESH_PX_REG_CAP (MESH_PX_SHARED + 4)
/* u32 the manifest's `scratch_bytes`, and u64 the scratch's device address on
 * this machine (0: none), written with the extension: a program's own device
 * memory (CUDA: cuMemAlloc'd in the context that runs it -- meshd's, or the
 * MPS worker's; Metal: a private-to-the-program MTLBuffer bound at index 3),
 * zeroed at load, kept while the image is loaded, never mapped to the host
 * (`ctx.scratch_view()`, `scratch_ptr()`; a GEMM's matrices, a model's weights).  The CPU executors
 * have none. */
#define MESH_PX_SCRATCH_BYTES (MESH_PX_REG_CAP + 4)
#define MESH_PX_SCRATCH (MESH_PX_SCRATCH_BYTES + 8)
/* The largest manifest `scratch_bytes` (1 GiB). */
#define MESH_MAX_SCRATCH_BYTES ((uint32_t)(1 << 30))
#define MESH_FD_NAME 0x00 /* [u8; 20], NUL padded */
#define MESH_FD_NAME_MAX 19
#define MESH_FD_TYPE 0x14 /* FT_* */
#define MESH_FD_OFF 0x18 /* view offset */
#define MESH_FT_U32 0u
#define MESH_FT_I32 1u
#define MESH_FT_F32 2u
#define MESH_FT_U64 3u
#define MESH_FT_U8 4u
#define MESH_FIELD_TYPES_LEN 5
extern const char *const MESH_FIELD_TYPES[5];
#define MESH_EXT_FIELDS_LEN 15
extern const mesh_field MESH_EXT_FIELDS[15];

/* Slot s's manifest extension. */
static inline size_t mesh_prog_ext(uint32_t slot) { return MESH_PROG_EXT + MESH_PX_BYTES * (size_t)slot; }

#endif
