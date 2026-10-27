/* @ajax/mesh-abi-placement: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/placement.h"

const char *const MESH_PLACE_CAPS[3] = {"none", "pin", "split"};

const char *const MESH_PLACES[3] = {"local", "pin", "split"};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_SH_BUF + MESH_SH_BUF_BYTES * 2 <= MESH_SH_DATA, "SH_BUF + SH_BUF_BYTES * 2 <= SH_DATA");
_Static_assert(MESH_SHB_SYS + 4 <= MESH_SH_BUF_BYTES, "SHB_SYS + 4 <= SH_BUF_BYTES");
_Static_assert(MESH_PLACE_PARTS_MAX == 32, "PLACE_PARTS_MAX == 32");
