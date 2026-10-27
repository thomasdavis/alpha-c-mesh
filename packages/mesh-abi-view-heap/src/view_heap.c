/* @ajax/mesh-abi-view-heap: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/view_heap.h"

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_VIEW_HEAP % MESH_VIEW_PAGE == 0), "VIEW_HEAP.is_multiple_of(VIEW_PAGE)");
_Static_assert(MESH_VIEW_PAGES == 0x700, "VIEW_PAGES == 0x700");
