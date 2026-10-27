/* gemm's init and task (src/main.c runs them as a native worker). */
#ifndef GEMM_H
#define GEMM_H
#include "mesh/worker_ctx.h"
#include <stddef.h>
#define GEMM_MAGIC 0x4d4d4547u /* "GEMM" */
#define GEMM_V_SIZE 0x04u
#define GEMM_V_N 0x08u
#define GEMM_V_US 0x0cu
#define GEMM_V_GFLOPS 0x10u
#define GEMM_V_BEST 0x14u
#define GEMM_V_PLAIN 0x18u
#define GEMM_V_SINGLE 0x1cu
#define GEMM_V_IMPL 0x20u
#define GEMM_V_THREADS 0x24u
#define GEMM_V_RUNS 0x28u
#define GEMM_V_C 0x1000u
#define GEMM_CHECK_MAX 64u
#define GEMM_N_MAX 8192u
#define GEMM_OP_CHECK 3u
#define GEMM_OP_PLAIN 4u
#define GEMM_OP_SINGLE 5u
#define GEMM_OP_THREADS 6u
/* The size the job items (OP_JOB) multiply. */
#define GEMM_OP_SIZE 7u
/* The job items' records (manifest job_out): n u32, GFLOP/s f32, at V_JOB_OUT + 8 (ticket % 64). */
#define GEMM_V_JOB_OUT 0x6000u
/* 2: the blocked C GEMM (1, Accelerate, is the Rust's macOS path: not ported). */
#define GEMM_IMPL 2u
void gemm_init(const mesh_worker_ctx *ctx, void *user);
uint32_t gemm_task(const mesh_worker_ctx *ctx, uint32_t op, uint32_t arg, void *user);
/* C = A B (n x n, row-major) with the blocked kernel over `threads()` threads (single: one). */
void gemm_fast(size_t n, const float *a, const float *b, float *c, bool single);
/* The plain loop: what a straightforward C GEMM does on one core (i, j, k; no blocking). */
void gemm_plain(size_t n, const float *a, const float *b, float *c);
size_t gemm_threads(void);
#endif
