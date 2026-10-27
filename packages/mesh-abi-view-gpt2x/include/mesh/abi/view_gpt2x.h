/* @ajax/mesh-abi-view-gpt2x: programs/gpt2x's view: GPT-2 exact-mode kernels' parameter block, ops and epilogues.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_GPT2X_H
#define MESH_ABI_VIEW_GPT2X_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* sha256(sha256(64 zero bytes)) = e2f61c3f71d1defd... (Python hashlib). */
/* programs/gpt2x: GPT-2 124M's exact-mode kernels on the GPU (CUDA and
 * Metal; mesh_ref::gpt2 is the reference, mesh-host `gpt2x` the host side).
 * The weights, the KV cache and the activations live in the program's
 * scratch as f32s; offsets below are in f32s of the scratch.  A stage's
 * operands are written to the parameter block before its tasks are
 * published (the task's arg is only its unit of work); the tokens of an
 * EMBED and the bytes of a PUT or COPY go through the view. */
#define MESH_GPT2X_X_MAGIC 0x00u /* "GPX2" after OP_INIT */
#define MESH_GPT2X_MAGIC 0x32585047u
/* The parameter block: PARAM_WORDS u32s (word i at X_PARAMS + 4 i). */
#define MESH_GPT2X_X_PARAMS 0x100u
#define MESH_GPT2X_PARAM_WORDS 24u
/* Tokens of an EMBED (u32 x CTX). */
#define MESH_GPT2X_X_TOKENS 0x1000u
/* The data window of PUT and COPY: WINDOW_FLOATS f32s. */
#define MESH_GPT2X_X_WINDOW 0x10000u
#define MESH_GPT2X_VIEW_BYTES 0x700000u
#define MESH_GPT2X_WINDOW_FLOATS ((uint32_t)((MESH_GPT2X_VIEW_BYTES - MESH_GPT2X_X_WINDOW) / 4))
/* f32s a PUT or COPY task moves (its arg is the chunk). */
#define MESH_GPT2X_CHUNK 65536u
/* Columns a GEMV task computes (mesh-rt linalg::GEMV_COLS). */
#define MESH_GPT2X_GEMV_COLS 64u
/* Vocabulary rows a LOGITS task computes (32 a warp, 4 at a time). */
#define MESH_GPT2X_LOGIT_ROWS 256u
/* Parameter words. */
#define MESH_GPT2X_P_A 0u /* GEMM/GEMV A, NORM src, LOGITS h */
#define MESH_GPT2X_P_B 1u /* GEMM/GEMV B, NORM gain, LOGITS wte */
#define MESH_GPT2X_P_BIAS 2u /* GEMM/GEMV bias, NORM shift */
#define MESH_GPT2X_P_C 3u /* GEMM/GEMV C, NORM dst, LOGITS out */
#define MESH_GPT2X_P_M 4u
#define MESH_GPT2X_P_K 5u
#define MESH_GPT2X_P_N 6u
#define MESH_GPT2X_P_EPI 7u /* EPI_* */
#define MESH_GPT2X_P_T 8u /* rows of the stage (tokens) */
#define MESH_GPT2X_P_P0 9u /* position of row 0 */
#define MESH_GPT2X_P_QKV 10u /* KV, ATTN: the q|k|v rows (T x 3 WIDTH) */
#define MESH_GPT2X_P_KC 11u /* KV, ATTN: this layer's K cache (CTX x WIDTH) */
#define MESH_GPT2X_P_VC 12u /* KV, ATTN: this layer's V cache */
#define MESH_GPT2X_P_Y 13u /* ATTN: out (T x WIDTH) */
#define MESH_GPT2X_P_WTE 14u /* EMBED */
#define MESH_GPT2X_P_WPE 15u /* EMBED */
#define MESH_GPT2X_P_X 16u /* EMBED: out (T x WIDTH) */
#define MESH_GPT2X_P_OFF 17u /* PUT, COPY: scratch offset */
#define MESH_GPT2X_P_LEN 18u /* PUT, COPY: f32s */
#define MESH_GPT2X_P_QB 19u /* ATTN: queries a task */
/* Epilogues of GEMM and GEMV (mesh_ref::gpt2::Epi). */
#define MESH_GPT2X_EPI_BIAS 0u
#define MESH_GPT2X_EPI_BIAS_GELU 1u
#define MESH_GPT2X_EPI_BIAS_RESIDUAL 2u
/* window[chunk] -> scratch[P_OFF + CHUNK chunk ..] (P_LEN f32s in all). */
#define MESH_GPT2X_OP_PUT 3u
/* scratch[P_OFF + CHUNK chunk ..] -> window[chunk]. */
#define MESH_GPT2X_OP_COPY 4u
/* arg row r: x[r] = wte[tokens[r]] + wpe[P_P0 + r]. */
#define MESH_GPT2X_OP_EMBED 5u
/* arg row r: C[r] = layernorm(A[r]) * B + BIAS (rows of WIDTH). */
#define MESH_GPT2X_OP_NORM 6u
/* arg tile: C = A (M x K) B (K x N) through the epilogue (64 x 64 tiles). */
#define MESH_GPT2X_OP_GEMM 7u
/* arg r * ceil(N / GEMV_COLS) + column block: the same for one row. */
#define MESH_GPT2X_OP_GEMV 8u
/* arg row r: the K and V thirds of QKV row r -> the caches at P_P0 + r. */
#define MESH_GPT2X_OP_KV 9u
/* arg head * ceil(T / QB) + block: causal attention of QB query rows of one head. */
#define MESH_GPT2X_OP_ATTN 10u
/* arg block: out[256 block ..] = wte rows times h (the warp-strided GEMV). */
#define MESH_GPT2X_OP_LOGITS 11u
/* Counters: 0 tasks, 1 rows normalized, 2 tiles, 3 chunks moved. */
#define MESH_GPT2X_C_TASKS 0u

#endif
