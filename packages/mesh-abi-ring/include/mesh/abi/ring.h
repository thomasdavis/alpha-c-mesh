/* @ajax/mesh-abi-ring: A program's ticket ring and its completion results and marks.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_RING_H
#define MESH_ABI_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ring: PROG_RING entries {op, arg, slot, seq}, ticket t at P_RING + ENTRY_BYTES * (t % PROG_RING) */
#define MESH_P_RING 0x200
#define MESH_PROG_RING 64u
#define MESH_ENTRY_BYTES 16
#define MESH_E_OP 0x0
#define MESH_E_ARG 0x4
#define MESH_E_SLOT 0x8 /* completion slot (< PROG_RESULTS) */
#define MESH_E_SEQ 0xc /* t + 1 when published */
/* Completion: result at P_RESULTS + 4 slot, then P_MARKS + 4 slot =
 * t + 1 (release), then P_DONE += 1.  The host never publishes ticket
 * t + PROG_RING before t completed.  After a halt (every node gone) meshd
 * rewinds P_RING_HEAD to its published tail before relaunching. */
#define MESH_P_RESULTS 0x600
#define MESH_P_MARKS 0x700
#define MESH_PROG_RESULTS 64u

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_P_RING + MESH_ENTRY_BYTES * MESH_PROG_RING  <= MESH_P_RESULTS, "P_RING + ENTRY_BYTES * PROG_RING as usize <= P_RESULTS");
_Static_assert(MESH_P_RESULTS + 4 * MESH_PROG_RESULTS  <= MESH_P_MARKS, "P_RESULTS + 4 * PROG_RESULTS as usize <= P_MARKS");
_Static_assert(MESH_PROG_RING == MESH_PROG_RESULTS, "PROG_RING == PROG_RESULTS");

#endif
