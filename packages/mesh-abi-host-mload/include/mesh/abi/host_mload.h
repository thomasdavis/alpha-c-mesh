/* @ajax/mesh-abi-host-mload: Member load records (ML_*): each member's last load report.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_MLOAD_H
#define MESH_ABI_HOST_MLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* Member k's load at mload(k), MLOAD_BYTES: the last F_LOADREP it sent (its
 * own record: this machine's, at each sample).  Bytes ML_SHARE..MLOAD_BYTES
 * are the frame's body. */
#define MESH_S_MLOAD 0x1200
#define MESH_MLOAD_BYTES 0x40
#define MESH_ML_AT_MS 0x00 /* u64 this meshd's uptime ms when it arrived (0: never) */
#define MESH_ML_SHARE 0x08 /* f32 S_GPU_SHARE */
#define MESH_ML_SHARE10 0x0c /* f32 S_GPU_SHARE10 */
#define MESH_ML_MAX_GAP_US 0x10 /* f32 S_GPU_MAX_GAP_US */
#define MESH_ML_CPU 0x14 /* f32 S_CPU_UTIL */
#define MESH_ML_CPU_BIG 0x18 /* f32 */
#define MESH_ML_CPU_LITTLE 0x1c /* f32 */
#define MESH_ML_PSI_CPU 0x20 /* f32 */
#define MESH_ML_PSI_MEM 0x24 /* f32 */
#define MESH_ML_MESHD_CPU 0x28 /* f32 */
#define MESH_ML_MEM_AVAIL_MB 0x2c /* u32 */
#define MESH_ML_MEM_TOTAL_MB 0x30 /* u32 */
#define MESH_ML_DISK_FREE_MB 0x34 /* u32 */
#define MESH_ML_CPUS 0x38 /* u32 big | little << 16 */
#define MESH_ML_FLAGS 0x3c /* u32 its LF_* */
/* F_LOADREP's body: a member record from ML_SHARE on. */
#define MESH_LOADREP_BYTES (MESH_MLOAD_BYTES - MESH_ML_SHARE)
#define MESH_MLOAD_FIELDS_LEN 15
extern const mesh_field MESH_MLOAD_FIELDS[15];

/* Member k's load record. */
static inline size_t mesh_mload(uint32_t k) { return MESH_S_MLOAD + MESH_MLOAD_BYTES * (size_t)k; }

#endif
