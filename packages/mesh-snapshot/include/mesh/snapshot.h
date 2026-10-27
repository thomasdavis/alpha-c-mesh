/* @ajax/mesh-snapshot: the snapshot sent to viewers (status.rs `snapshot`;
 * mesh-abi "snapshot v3"): a header, the mailbox header, one section per
 * non-empty slot in ascending order (its table entry, control block,
 * extension, node records and its view's snapshot prefix, zero padded), then
 * the host block with the load regions. */
#ifndef MESH_SNAPSHOT_H
#define MESH_SNAPSHOT_H

#include "mesh/buf.h"
#include "mesh/host_block.h"
#include "mesh/mailbox.h"

/* Appends one snapshot to out. */
void mesh_snapshot(mesh_buf *out, const mesh_mailbox *mb, mesh_host_block *host);

#endif
