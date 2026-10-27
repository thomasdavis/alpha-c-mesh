/* @ajax/mesh-abi-host-pload: Program load records (PL_*): GPU share, latency, throughput and CPU cost per slot.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_PLOAD_H
#define MESH_ABI_HOST_PLOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* Per program slot s at pload(s), PLOAD_BYTES (zero for an empty slot). */
#define MESH_S_PLOAD 0x1800
#define MESH_PLOAD_BYTES 0x80
#define MESH_PL_FLAGS 0x00 /* u32 PLF_* */
#define MESH_PL_LAT_N 0x04 /* u32 trace records behind the latency figures */
#define MESH_PL_SHARE 0x08 /* f32 (1 s) the GPU share its idle nodes saw (PLF_GPU) */
#define MESH_PL_SHARE10 0x0c /* f32 (10 s) */
#define MESH_PL_SLICE_US 0x10 /* f32 (10 s) */
#define MESH_PL_GAP_US 0x14 /* f32 (10 s) */
#define MESH_PL_MAX_GAP_US 0x18 /* f32 (1 s) */
#define MESH_PL_OBS 0x1c /* f32 (1 s) node-seconds observed per second */
/* f32 task time quantiles and maximum, us, from the trace (end - start of
 * each record; the last LOAD_WINDOW samples' records, at most TRACE_PER_PROG a sample). */
#define MESH_PL_P50_US 0x20
#define MESH_PL_P90_US 0x24
#define MESH_PL_P99_US 0x28
#define MESH_PL_MAX_US 0x2c
/* f32 (10 s) mean node busy time per task, us (N_BUSY_NS / N_TASKS deltas):
 * a placed program's time per part (`split auto` weighs parts with it). */
#define MESH_PL_TASK_US 0x30
#define MESH_PL_TASKS_S 0x34 /* f32 (10 s) tasks completed per second */
#define MESH_PL_BUSY 0x38 /* f32 (1 s) node occupancy: busy ns / (nodes x wall) */
#define MESH_PL_QUEUE_US 0x3c /* f32 (10 s) Metal: mean commit -> GPU start */
/* PL_HOST..PLOAD_BYTES: the CPU executors' cost (wasm lanes: each lane
 * thread's own CPU time, CLOCK_THREAD_CPUTIME_ID, and its linear memory;
 * native workers: their processes' CPU time and RSS).  PLF_HOST says they
 * are measured; zero for a GPU slot. */
#define MESH_PL_HOST 0x40
#define MESH_PL_CPU_NS 0x40 /* u64 CPU ns since the image's load */
#define MESH_PL_RSS 0x48 /* u64 resident bytes now (wasm: linear memory; native: the processes' RSS, a retained image's included) */
#define MESH_PL_CPU 0x50 /* f32 (1 s) CPU seconds per second (1.0: one core busy) */
#define MESH_PL_CPU10 0x54 /* f32 (10 s) */
#define MESH_PL_CPU_TASK_US 0x58 /* f32 (10 s) CPU us per completed task (0: no tasks) */
#define MESH_PL_UNITS 0x5c /* u32 lanes running (wasm) / live worker processes (native) */
/* 0x60..0x80: free. */
#define MESH_PLF_LOADED 1u
#define MESH_PLF_GPU 2u /* the share fields were observed */
#define MESH_PLF_LAT 4u /* the latency fields have records */
#define MESH_PLF_TASK 8u /* PL_TASK_US has tasks */
#define MESH_PLF_HOST 0x10u /* PL_HOST's fields are measured (a wasm or native slot) */
#define MESH_PLOAD_FIELDS_LEN 22
extern const mesh_field MESH_PLOAD_FIELDS[22];

/* Slot s's program load record. */
static inline size_t mesh_pload(uint32_t s) { return MESH_S_PLOAD + MESH_PLOAD_BYTES * (size_t)s; }

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_PL_QUEUE_US + 4 <= MESH_PL_HOST, "PL_QUEUE_US + 4 <= PL_HOST");
_Static_assert(MESH_PL_CPU_NS == MESH_PL_HOST, "PL_CPU_NS == PL_HOST");
_Static_assert((MESH_PL_CPU_NS % 8 == 0), "PL_CPU_NS.is_multiple_of(8)");
_Static_assert((MESH_PL_RSS % 8 == 0), "PL_RSS.is_multiple_of(8)");
_Static_assert(0x1800 <= MESH_S_PLOAD, "0x1800 <= S_PLOAD");
_Static_assert((MESH_S_PLOAD % 8 == 0), "S_PLOAD.is_multiple_of(8)");

#endif
