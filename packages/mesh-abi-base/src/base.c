/* @ajax/mesh-abi-base: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/base.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_PROG_SLOTS <= 32, "PROG_SLOTS <= 32");
_Static_assert(MESH_MAX_SYSTEMS <= 16, "MAX_SYSTEMS <= 16");
_Static_assert((MESH_RT_SHARED_BYTES % MESH_SHARED_ALIGN == 0), "RT_SHARED_BYTES.is_multiple_of(SHARED_ALIGN)");
_Static_assert((MESH_MAX_SHARED_BYTES % MESH_SHARED_ALIGN == 0), "MAX_SHARED_BYTES.is_multiple_of(SHARED_ALIGN)");
_Static_assert(MESH_MAX_WARPS * MESH_NODE_THREADS == 1024, "MAX_WARPS * NODE_THREADS == 1024");
