/* @ajax/mesh-abi-ops: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/ops.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_OP_JOB > MESH_OP_REQ, "OP_JOB > OP_REQ");
_Static_assert(MESH_OP_JOB < MESH_OP_FOLLOW, "OP_JOB < OP_FOLLOW");
_Static_assert(MESH_R_JOB_FAIL == MESH_R_BAD_OP, "R_JOB_FAIL == R_BAD_OP");
_Static_assert(MESH_OP_FOLLOW > MESH_OP_REQ, "OP_FOLLOW > OP_REQ");
