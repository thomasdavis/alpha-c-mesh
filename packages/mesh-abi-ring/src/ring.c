/* @ajax/mesh-abi-ring: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/ring.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_P_RING + MESH_ENTRY_BYTES * MESH_PROG_RING  <= MESH_P_RESULTS, "P_RING + ENTRY_BYTES * PROG_RING as usize <= P_RESULTS");
_Static_assert(MESH_P_RESULTS + 4 * MESH_PROG_RESULTS  <= MESH_P_MARKS, "P_RESULTS + 4 * PROG_RESULTS as usize <= P_MARKS");
_Static_assert(MESH_PROG_RING == MESH_PROG_RESULTS, "PROG_RING == PROG_RESULTS");
