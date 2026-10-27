/* @ajax/mesh-abi-peer-sizes: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/peer_sizes.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_JOB_HEAD == 4, "JOB_HEAD == 4");
_Static_assert(MESH_F_BLOB_MAX <= MESH_F_JOB_MAX, "F_BLOB_MAX <= F_JOB_MAX");
