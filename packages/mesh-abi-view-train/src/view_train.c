/* @ajax/mesh-abi-view-train: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/view_train.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_TRAIN_T_LOSS + 4 * 256 <= MESH_TRAIN_VIEW_BYTES, "train::T_LOSS + 4 * 256 <= train::VIEW_BYTES");
_Static_assert(MESH_TRAIN_T_Y + 4 * MESH_TRAIN_SAMPLES <= MESH_TRAIN_T_LOSS, "train::T_Y + 4 * train::SAMPLES <= train::T_LOSS");
_Static_assert(MESH_TRAIN_T_STEP_HI + 4 <= MESH_TRAIN_T_X, "train::T_STEP_HI + 4 <= train::T_X");
