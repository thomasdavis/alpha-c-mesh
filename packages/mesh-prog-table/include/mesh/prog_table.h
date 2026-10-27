/* @ajax/mesh-prog-table: the program table as meshd writes it (programs.rs).
 *
 * meshd is the table's only writer. It rewrites an entry only while that
 * slot's kernel is not running (load, upgrade, rollback, reset happen with it
 * halted), state changes excepted. Rewrites go under the entry's PT_GEN
 * seqlock (odd while writing); every change bumps H_PROG_SEQ so viewers know
 * to re-read. Readers on any thread take a stable copy (read_entry). */
#ifndef MESH_PROG_TABLE_H
#define MESH_PROG_TABLE_H

#include "mesh/abi.h"
#include "mesh/mailbox.h"

#include <stdbool.h>
#include <stdint.h>

/* Rewrites slot s's entry (all but PT_STATE/PT_GEN), its manifest extension
 * and its state under the seqlock: PT_GEN odd, the bytes, PT_GEN even. */
void mesh_pt_write_entry(const mesh_mailbox *mb, uint32_t s, const uint8_t entry[MESH_PT_BYTES], const uint8_t ext[MESH_PX_BYTES],
                         uint32_t state);
/* The state word alone (it may change while the kernel runs); unchanged: nothing. */
void mesh_pt_set_state(const mesh_mailbox *mb, uint32_t s, uint32_t state);
/* An EMPTY slot: the entry, extension and pipe block zeroed (seqlocked). */
void mesh_pt_clear(const mesh_mailbox *mb, uint32_t s);
/* Slot s's pipe block (PP_*), under its seqlock, in place while it runs.
 * PP_FLIGHT and PP_FLIGHT1 are the executor's words: never written. Unchanged: nothing. */
void mesh_pt_write_pipe(const mesh_mailbox *mb, uint32_t s, const uint8_t b[MESH_PP_BYTES]);
/* Holds (or releases) slot s's inbox: pushes answer "full" while held. */
void mesh_pt_set_hold(const mesh_mailbox *mb, uint32_t s, bool on);
/* Producer slot s's pushes in flight counted in its word of `parity`. */
uint32_t mesh_pt_flight(const mesh_mailbox *mb, uint32_t s, uint32_t parity);
/* Starts a quiescence period (H_PIPE_EPOCH bumped, then a fence): the old parity, whose words now only drain. */
uint32_t mesh_pt_flight_epoch(const mesh_mailbox *mb);
/* Slot s runs no code: its pushes in flight are none. */
void mesh_pt_flight_clear(const mesh_mailbox *mb, uint32_t s);
/* Fresh per-slot words for a new program: control block (all but the GPU's P_RUN_REQ), node records, trace. */
void mesh_pt_reset_slot(const mesh_mailbox *mb, uint32_t s);
/* The name as the table stores it (NUL-terminated; invalid UTF-8 replaced as Rust's from_utf8_lossy would not matter: names are [a-z0-9-]). */
void mesh_pt_name_of(const mesh_mailbox *mb, uint32_t s, char out[MESH_NAME_BYTES + 1]);
/* The non-empty slot whose entry holds name; false when none. */
bool mesh_pt_find(const mesh_mailbox *mb, const char *name, uint32_t *slot);
/* One stable copy of slot s's entry (retries while the writer holds the seqlock). */
void mesh_pt_read_entry(const mesh_mailbox *mb, uint32_t s, uint8_t out[MESH_PT_BYTES]);

#endif
