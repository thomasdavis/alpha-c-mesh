/* @ajax/mesh-abi-snapshot: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/snapshot.h"

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_SC_NODE_RECS % 8 == 0), "SC_NODE_RECS.is_multiple_of(8)");
