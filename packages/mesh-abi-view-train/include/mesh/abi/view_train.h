/* @ajax/mesh-abi-view-train: programs/train's view: SGD of a cubic, lockstep.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_TRAIN_H
#define MESH_ABI_VIEW_TRAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* peer = lockstep: each step's gradient sums go through the exchange page. */
#define MESH_TRAIN_T_PARAMS 0x00u /* f32 x 4: a b c d */
#define MESH_TRAIN_T_LR 0x10u /* f32 (OP_INIT: LR; op OP_SET_LR changes it) */
#define MESH_TRAIN_T_STEPS_PER_TASK 0x14u /* u32 (OP_INIT: STEPS_PER_TASK) */
#define MESH_TRAIN_T_INV_BATCH 0x18u /* f32: 1/32, or 1/64 when peered at OP_INIT */
#define MESH_TRAIN_T_NEXT_STEP 0x1cu /* u32 low half of the next step (the task arg) */
#define MESH_TRAIN_T_ERR 0x20u /* u32 1: the last task timed out waiting for the peer */
#define MESH_TRAIN_T_PENDING 0x24u /* u32 1: a round is published and not yet taken */
#define MESH_TRAIN_T_ROUND 0x28u /* u32 low half of the pending round */
#define MESH_TRAIN_T_STEP_HI 0x2cu /* u32 high half of the next step */
#define MESH_TRAIN_T_X 0x100u /* f32 x SAMPLES */
#define MESH_TRAIN_T_Y 0x500u /* f32 x SAMPLES */
#define MESH_TRAIN_T_LOSS 0x900u /* f32 x 256: step t's batch loss at t mod 256 */
#define MESH_TRAIN_SAMPLES 256u
#define MESH_TRAIN_VIEW_BYTES 0x1000u
#define MESH_TRAIN_LR 0.005f
#define MESH_TRAIN_STEPS_PER_TASK 64u
/* op 3: T_LR = f32::from_bits(arg); returns 0. */
#define MESH_TRAIN_OP_SET_LR 3u
/* Counters: 0 steps, 1 peer timeouts, 2 inits. */
#define MESH_TRAIN_C_STEPS 0u
#define MESH_TRAIN_C_TIMEOUTS 1u
#define MESH_TRAIN_C_INITS 2u

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_TRAIN_T_LOSS + 4 * 256 <= MESH_TRAIN_VIEW_BYTES, "train::T_LOSS + 4 * 256 <= train::VIEW_BYTES");
_Static_assert(MESH_TRAIN_T_Y + 4 * MESH_TRAIN_SAMPLES <= MESH_TRAIN_T_LOSS, "train::T_Y + 4 * train::SAMPLES <= train::T_LOSS");
_Static_assert(MESH_TRAIN_T_STEP_HI + 4 <= MESH_TRAIN_T_X, "train::T_STEP_HI + 4 <= train::T_X");

#endif
