/* @ajax/mesh-abi-pipe-table: The pipe block (PP_*): a slot's inbox, connected feeds and pushes in flight.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PIPE_TABLE_H
#define MESH_ABI_PIPE_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"
#include "mesh/abi/fields.h"

/* ---------------------------------------------------------------- views */
/* ---------------------------------------------------------------- pipes */
/* The view heap: meshd allocates each program's view as whole pages,
 * first fit; a view keeps its offset across an upgrade when view_abi and
 * view_bytes are unchanged.
 * Per slot, PP_BYTES at `pipe_entry(s)`: its inbox (manifest `inbox`,
 * `inbox_type`) and the pipes meshd connected (manifest `feeds`; v2::pipe).
 * Host-written with the table entry, under the slot's PT_GEN seqlock; a
 * runtime reads it at every push/pop (feeds connect and disconnect while
 * programs run: no relaunch). */
#define MESH_PIPE_TABLE 0x4A000
#define MESH_PP_BYTES 0x40
#define MESH_PP_IN_OFF 0x00 /* u32 the inbox's offset in the slot's view */
#define MESH_PP_IN_CAP 0x04 /* u32 records (a power of two); 0: no inbox */
#define MESH_PP_IN_REC 0x08 /* u32 bytes a record (a multiple of 4) */
#define MESH_PP_IN_TYPE 0x0c /* u32 pipe::type_hash of inbox_type */
#define MESH_PP_FEEDS_MASK 0x10 /* u32 bit c: this slot may push into slot c's inbox (connected) */
#define MESH_PP_FED_MASK 0x14 /* u32 bit p: slot p's pushes into this inbox are connected */
/* u32 host: nonzero while meshd rewrites this inbox in place (a reset):
 * pushes answer "full" (pipe::PipeErr::Busy).  Under PT_GEN like the rest. */
#define MESH_PP_IN_HOLD 0x18
/* u32 x 2: this slot's pushes in progress, any inbox, counted in the word of
 * parity H_PIPE_EPOCH & 1 (pipe::flight_begin/end; `flight_word`):
 * read-modify-written by the slot's own executor only; meshd reads them, and
 * zeroes them only once the slot runs no code.  Not part of the block meshd
 * rewrites (`write_pipe` leaves them). */
#define MESH_PP_FLIGHT 0x1c
#define MESH_PP_FLIGHT1 0x30
#define MESH_PP_TYPE_NAME 0x20 /* [u8; NAME_BYTES] inbox_type, NUL padded */
#define MESH_PIPE_FIELDS_LEN 10
extern const mesh_field MESH_PIPE_FIELDS[10];

/* Slot s's pipe block. */
static inline size_t mesh_pipe_entry(uint32_t slot) { return MESH_PIPE_TABLE + MESH_PP_BYTES * (size_t)slot; }
/* Slot s's flight word of parity `parity` (0 or 1). */
static inline size_t mesh_flight_word(uint32_t s, uint32_t parity) {
    return mesh_pipe_entry(s) + ((parity & 1u) == 0 ? MESH_PP_FLIGHT : MESH_PP_FLIGHT1);
}

#endif
