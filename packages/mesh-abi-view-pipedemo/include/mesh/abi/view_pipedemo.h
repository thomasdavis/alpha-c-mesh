/* @ajax/mesh-abi-view-pipedemo: The pipe demo's records and views: msg, digest, sources, pipehash, sinks.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_VIEW_PIPEDEMO_H
#define MESH_ABI_VIEW_PIPEDEMO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sha256_compress.h"
#include "hist_quantile.h"

/* The pipe demo (docs/plan-fleet-scheduling.md F2; README "Pipes"): a CPU
 * stage feeds a GPU stage, whose results a CPU stage checks, all through
 * pipes (v2::pipe):
 *
 * pipesrc (wasm) / pipesrc-n (native) --msg--> pipehash (CUDA / Metal) --digest--> pipesink (native) / pipesink-w (wasm)
 * and, `mode 1`, a source straight into a sink (it hashes on the CPU itself).
 *
 * A `msg` (64 bytes) is (seq, the source's host-clock stamp, origin, 12
 * payload words derived from seq); its `digest` (48 bytes) is (seq, stamp,
 * origin | path, sha256d of the msg's 64 bytes).  A sink rebuilds the msg
 * from seq, checks the digest against its own SHA-256 and the record's stamp
 * against the host clock: one clock for every executor (CLOCK_MONOTONIC;
 * macOS CLOCK_UPTIME_RAW), so the latency is the whole pipeline's. */
#define MESH_PIPEDEMO_MSG_TYPE "msg"
#define MESH_PIPEDEMO_MSG_BYTES 64u
#define MESH_PIPEDEMO_DIGEST_TYPE "digest"
#define MESH_PIPEDEMO_DIGEST_BYTES 48u
/* Record words. */
#define MESH_PIPEDEMO_R_SEQ 0
#define MESH_PIPEDEMO_R_STAMP_LO 1
#define MESH_PIPEDEMO_R_STAMP_HI 2
/* ORIGIN_* | PATH_* (a digest's path bits say who hashed it). */
#define MESH_PIPEDEMO_R_ORIGIN 3
#define MESH_PIPEDEMO_R_PAYLOAD 4
#define MESH_PIPEDEMO_ORIGIN_WASM 1u
#define MESH_PIPEDEMO_ORIGIN_NATIVE 2u
#define MESH_PIPEDEMO_PATH_GPU 0x100u
/* Sources (pipesrc, pipesrc-n): the view (0x100 bytes; no inbox). */
#define MESH_PIPEDEMO_S_MODE 0x00u /* u32 MODE_* */
#define MESH_PIPEDEMO_S_BATCH 0x04u /* u32 records a task at most */
#define MESH_PIPEDEMO_S_INTERVAL_US 0x08u /* u32 0: as fast as the pipe takes them; else one batch per interval */
#define MESH_PIPEDEMO_S_NEXT_SEQ 0x0cu /* u32 */
#define MESH_PIPEDEMO_S_SENT 0x10u /* u32 records pushed */
#define MESH_PIPEDEMO_S_FULL 0x14u /* u32 pushes refused (the pipe was full: backpressure) */
#define MESH_PIPEDEMO_S_GONE 0x18u /* u32 tasks with no consumer to push to */
#define MESH_PIPEDEMO_S_LAST_NS 0x20u /* u64 the last batch's time */
#define MESH_PIPEDEMO_S_DST 0x28u /* u32 the slot pushed to last + 1 (0: none) */
#define MESH_PIPEDEMO_MODE_GPU 0u /* msgs to pipehash */
#define MESH_PIPEDEMO_MODE_DIRECT 1u /* hashed here, digests straight to a sink */
#define MESH_PIPEDEMO_MODE_OFF 2u
/* ops (on demand, `task <src> <op> <arg>`): set mode / batch / interval; reset the counters. */
#define MESH_PIPEDEMO_OP_MODE 2u
#define MESH_PIPEDEMO_OP_BATCH 3u
#define MESH_PIPEDEMO_OP_INTERVAL 4u
#define MESH_PIPEDEMO_OP_RESET 5u
#define MESH_PIPEDEMO_SRC_BATCH 64u
/* pipehash: the view, its inbox of msgs at H_INBOX (1024 records), and
 * per node a carry: the digests a full (or gone) sink did not take yet,
 * pushed first by the node's next task (nothing is dropped).
 * Counters: 0 hashed (pushed on), 1 stalls (a task that found the sink
 * without room and carried or waited), 2 carried (digests put in a carry). */
#define MESH_PIPEDEMO_H_DST 0x0cu /* u32 the sink's slot + 1 (0: none found) */
#define MESH_PIPEDEMO_H_INBOX 0x80u
#define MESH_PIPEDEMO_H_CAP 1024u
#define MESH_PIPEDEMO_H_CARRY 0x12200u /* node n's carry at H_CARRY + n * H_CARRY_BYTES: u32 count, then (16-aligned) up to 32 digests */
#define MESH_PIPEDEMO_H_CARRY_BYTES 0x610u
#define MESH_PIPEDEMO_H_CARRY_REC 0x10u
#define MESH_PIPEDEMO_H_NODES_MAX 8u
/* Sinks (pipesink, pipesink-w): the view, the inbox of digests at K_INBOX. */
#define MESH_PIPEDEMO_K_RECORDS 0x00u /* u32 digests taken */
#define MESH_PIPEDEMO_K_BAD 0x04u /* u32 of those, the ones whose sha256d did not match */
#define MESH_PIPEDEMO_K_GPU 0x08u /* u32 ... hashed on the GPU (PATH_GPU) */
#define MESH_PIPEDEMO_K_CPU 0x0cu /* u32 ... hashed by a source */
#define MESH_PIPEDEMO_K_MAX_US 0x10u /* u32 the largest latency seen */
#define MESH_PIPEDEMO_K_P50_US 0x14u /* u32 (refreshed every K_STATS_EVERY tasks of node 0, and on op OP_STATS) */
#define MESH_PIPEDEMO_K_P90_US 0x18u
#define MESH_PIPEDEMO_K_P99_US 0x1cu
#define MESH_PIPEDEMO_K_EMPTY 0x20u /* u32 tasks that found the inbox empty */
#define MESH_PIPEDEMO_K_LAST_BAD 0x40u /* [u32; 12] the last digest record that did not check */
#define MESH_PIPEDEMO_K_HIST 0x100u /* u32 x K_BINS: latency in 1 us bins (the last: >= K_BINS - 1 us) */
#define MESH_PIPEDEMO_K_BINS 4096u
#define MESH_PIPEDEMO_K_INBOX 0x4100u
#define MESH_PIPEDEMO_K_CAP 4096u
#define MESH_PIPEDEMO_K_STATS_EVERY 64u
/* ops: refresh the percentiles now; zero the counters and histogram. */
#define MESH_PIPEDEMO_OP_STATS 2u
#define MESH_PIPEDEMO_OP_SINK_RESET 3u

/* Payload word i (4..16) of msg seq. */
static inline uint32_t mesh_pipedemo_payload(uint32_t seq, size_t i) {
    return (seq ^ 0x5bd1e995u) * 0x9e3779b9u + (uint32_t)i * 0x85ebca6bu;
}
/* The msg of seq: (seq, stamp, origin, 12 payload words). */
void mesh_pipedemo_msg(uint32_t seq, uint64_t stamp, uint32_t origin, uint32_t out[16]);
/* sha256d of the msg's 64 bytes (its words little-endian), as the 8 state
 * words of the second hash (big-endian bytes of the digest). */
void mesh_pipedemo_sha256d_msg(const uint32_t m[16], uint32_t out[8]);
/* A digest record for msg m hashed on path (0 or PATH_GPU). */
void mesh_pipedemo_digest(const uint32_t m[16], uint32_t path, uint32_t out[12]);
/* `quantile` of the Rust is hist_quantile (@ajax/hist-quantile). */

#endif
