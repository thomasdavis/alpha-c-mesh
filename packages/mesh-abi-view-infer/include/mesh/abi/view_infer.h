/* @ajax/mesh-abi-view-infer: programs/infer's view: the trained cubic on a grid.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_INFER_H
#define MESH_ABI_VIEW_INFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* programs/infer: the trained cubic over a 64-point grid, reading train's
 * view through `Ctx::peer_view` (manifest `reads = ["train"]`). */
#define MESH_INFER_I_PARAMS 0x00u /* f32 x 4: the params the last run used */
#define MESH_INFER_I_RUNS 0x10u /* u32 runs that found train */
#define MESH_INFER_I_TRAIN_STEP 0x14u /* u32 train's T_NEXT_STEP at the last run */
#define MESH_INFER_I_X 0x100u /* f32 x POINTS: -1 + 2 i / (POINTS - 1) (OP_INIT) */
#define MESH_INFER_I_Y 0x300u /* f32 x POINTS */
#define MESH_INFER_POINTS 64u
#define MESH_INFER_VIEW_BYTES 0x1000u
/* op 3: probe `peer_view(arg)`: the view's length, or R_NO_VIEW (an
 * undeclared slot also records FAULT_PEER_VIEW: a test hook for `reads`). */
#define MESH_INFER_OP_PROBE_READ 3u
#define MESH_INFER_R_NO_VIEW 0xfffffffeu
/* Task result when train is not loaded (no run). */
#define MESH_INFER_R_NO_TRAIN 0u

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_INFER_I_Y + 4 * MESH_INFER_POINTS <= MESH_INFER_VIEW_BYTES, "infer::I_Y + 4 * infer::POINTS <= infer::VIEW_BYTES");
_Static_assert(MESH_INFER_I_X + 4 * MESH_INFER_POINTS <= MESH_INFER_I_Y, "infer::I_X + 4 * infer::POINTS <= infer::I_Y");

#endif
