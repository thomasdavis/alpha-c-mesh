/* @ajax/mesh-manifest-encode: the bytes a manifest puts in the mailbox, shared
 * by meshd and kcheck (mesh-host manifest.rs encode_entry, encode_ext). */
#ifndef MESH_MANIFEST_ENCODE_H
#define MESH_MANIFEST_ENCODE_H

#include "mesh/manifest.h"

#include "mesh/abi/ext.h"
#include "mesh/abi/program_table.h"

#include <stdint.h>

/* What the loader learned, beside the manifest. */
typedef struct {
    uint32_t view_off;
    uint32_t reads_mask;
    uint64_t ptx_hash;
    uint32_t regs;
    uint32_t code_bytes;
    uint32_t rt_abi;
    uint32_t loads;
} mesh_image;

/* The program-table entry (PT_*), with PT_STATE and PT_GEN zero: the writer
 * sets them under the seqlock. */
void mesh_manifest_encode_entry(const mesh_manifest *m, const mesh_image *im, uint8_t out[MESH_PT_BYTES]);
/* The manifest extension (PX_*): counter names and fields. */
void mesh_manifest_encode_ext(const mesh_manifest *m, uint8_t out[MESH_PX_BYTES]);

#endif
