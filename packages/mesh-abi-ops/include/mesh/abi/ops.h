/* @ajax/mesh-abi-ops: Task ops (task, init, req, follow, job) and their reserved results.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_OPS_H
#define MESH_ABI_OPS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ops: 0 the program's task, 1 init (once, on a fresh load or `reset`),
 * 2 a byte request through the window, 3.. program-defined. */
#define MESH_OP_TASK 0u
#define MESH_OP_INIT 1u
#define MESH_OP_REQ 2u
/* Reserved for placement (manifest `place = "split"`): render part `arg` of a
 * frame from the shared state meshd wrote into the view (the owner's, mirrored
 * over the peer link), consuming no input and advancing nothing.  Ops
 * 3..OP_FOLLOW are program-defined. */
#define MESH_OP_FOLLOW 0xffu
/* Reserved for jobs (plan F5; `policy = "job"`): process item
 * `base + arg` of a job, where `base` is the u64 meshd wrote at the view's
 * `job_base` offset (the item's high 32 bits: base = item & !0xffff_ffff,
 * arg = item as u32) before the item's ticket was published.  The result is
 * the item's u32 result (R_JOB_FAIL: the item failed); a program with
 * `job_out = "off:rec"` writes `rec` more bytes of result at view
 * `off + rec * (ticket % PROG_RESULTS)` before it returns (meshd reads them
 * when it collects the completion).  A job may name another op.  With
 * `job_payload = "off:len"` an item also writes `len` bytes of payload at view
 * `off + len * (ticket % PROG_RESULTS)`: meshd keeps them as the item's blob
 * (content-addressed, off the job log; the result carries its sha256, length
 * and holders: mesh-host blobs.rs, blobd.rs). */
#define MESH_OP_JOB 0xfeu
/* An item's task result that fails the item (it is retried, up to the job's
 * attempts, then recorded failed with the reason). */
#define MESH_R_JOB_FAIL 0xffffffffu
/* The result of op OP_INIT, and of any op a program does not define. */
#define MESH_R_NONE 0u
#define MESH_R_BAD_OP 0xffffffffu

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_OP_JOB > MESH_OP_REQ, "OP_JOB > OP_REQ");
_Static_assert(MESH_OP_JOB < MESH_OP_FOLLOW, "OP_JOB < OP_FOLLOW");
_Static_assert(MESH_R_JOB_FAIL == MESH_R_BAD_OP, "R_JOB_FAIL == R_BAD_OP");
_Static_assert(MESH_OP_FOLLOW > MESH_OP_REQ, "OP_FOLLOW > OP_REQ");

#endif
