/* @ajax/mesh-abi-view-heap: The view heap: whole pages meshd allocates to program views.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_HEAP_H
#define MESH_ABI_VIEW_HEAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/base.h"

#define MESH_VIEW_HEAP 0x100000
#define MESH_VIEW_HEAP_END (MESH_MAILBOX_BYTES)
#define MESH_VIEW_PAGE 0x1000
#define MESH_VIEW_PAGES ((MESH_VIEW_HEAP_END - MESH_VIEW_HEAP) / MESH_VIEW_PAGE)

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_VIEW_HEAP % MESH_VIEW_PAGE == 0), "VIEW_HEAP.is_multiple_of(VIEW_PAGE)");
_Static_assert(MESH_VIEW_PAGES == 0x700, "VIEW_PAGES == 0x700");

#endif
