/* @ajax/mesh-abi-snapshot: The snapshot v3 format (SN_*, SC_*): header, per-slot sections, host block.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_SNAPSHOT_H
#define MESH_ABI_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/program_table.h"
#include "mesh/abi/node.h"
#include "mesh/abi/ext.h"

/* ---------------------------------------------------------------- snapshot v3 */
/* v3: the host block grew to 0x3000 for the load regions (S_LOAD ..). */
#define MESH_SNAP_VERSION 3u
/* Variable length, little-endian:
 * SN_HEAD_BYTES header: SN_TOTAL u32 (all bytes), SN_COUNT u32 (sections),
 * SN_VERSION u32 (= SNAP_VERSION), SN_HOST_AT u32 (offset of the host block)
 * H_BYTES: mailbox [0, H_BYTES) (the header)
 * SN_COUNT sections, ascending slot, one per non-empty slot:
 * SC_HEAD_BYTES: SC_SLOT u32, SC_BYTES u32 (the whole section), SC_NODES u32
 * (node records copied), SC_VIEW u32 (view bytes copied = snap_bytes)
 * PT_BYTES       the program-table entry
 * SC_CTL_BYTES   the control block's first 4 lines (P_SHUTDOWN .. P_COUNTERS)
 * PX_BYTES       the manifest extension
 * SC_NODES x NODE_STRIDE node records
 * SC_VIEW bytes of view, zero padded to a multiple of 4
 * SNAP_HOST bytes: the host status block (S_*) */
#define MESH_SN_TOTAL 0x0
#define MESH_SN_COUNT 0x4
#define MESH_SN_VERSION 0x8
#define MESH_SN_HOST_AT 0xc
#define MESH_SN_HEAD_BYTES 0x10
#define MESH_SC_SLOT 0x0
#define MESH_SC_BYTES 0x4
#define MESH_SC_NODES 0x8
#define MESH_SC_VIEW 0xc
#define MESH_SC_HEAD_BYTES 0x10
#define MESH_SC_CTL_BYTES 0x200
/* Offsets within a section. */
#define MESH_SC_ENTRY (MESH_SC_HEAD_BYTES)
#define MESH_SC_CTL (MESH_SC_ENTRY + MESH_PT_BYTES)
#define MESH_SC_EXT (MESH_SC_CTL + MESH_SC_CTL_BYTES)
#define MESH_SC_NODE_RECS (MESH_SC_EXT + MESH_PX_BYTES)
#define MESH_SNAP_HOST 0x3000

/* A section's bytes: its head, entry, control lines, extension, `nodes`
 * node records and `view` view bytes rounded up to a multiple of 4. */
static inline size_t mesh_section_bytes(uint32_t nodes, uint32_t view) {
    return MESH_SC_NODE_RECS + MESH_NODE_STRIDE * (size_t)nodes + ((size_t)view + 3) / 4 * 4;
}

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_SC_NODE_RECS % 8 == 0), "SC_NODE_RECS.is_multiple_of(8)");

#endif
