/* @ajax/mesh-abi-view-infer: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/view_infer.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_INFER_I_Y + 4 * MESH_INFER_POINTS <= MESH_INFER_VIEW_BYTES, "infer::I_Y + 4 * infer::POINTS <= infer::VIEW_BYTES");
_Static_assert(MESH_INFER_I_X + 4 * MESH_INFER_POINTS <= MESH_INFER_I_Y, "infer::I_X + 4 * infer::POINTS <= infer::I_Y");
