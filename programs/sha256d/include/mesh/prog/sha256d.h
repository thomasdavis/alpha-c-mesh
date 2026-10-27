/* programs/sha256d's view layout and ops (mirrored by mesh-kcheck's sha checks).
 * Ported from alpha-rust-mesh programs/sha256d/src/lib.rs. */
#ifndef MESH_PROG_SHA256D_H
#define MESH_PROG_SHA256D_H

#define SHA_V_MAGIC 0x000u        /* u32 "S256" */
#define SHA_V_GEN 0x004u          /* u32 config generation, odd while it is written */
#define SHA_V_LOCK 0x008u         /* u32 spin lock (results, config) */
#define SHA_V_FOUND 0x00cu        /* u32 1 once a share was found (since the header/target/reset) */
#define SHA_V_HASHES 0x010u       /* u64 hashes done (since reset) */
/* 0x018: u64, free (was the OP_TASK chunk counter) */
#define SHA_V_BASE 0x020u         /* u32 nonce base */
#define SHA_V_CHUNK_LOG2 0x024u   /* u32 nonces per task = 1 << this */
#define SHA_V_TARGET_ZBITS 0x028u /* u32 leading zero bits of the target */
#define SHA_V_SHARES 0x02cu       /* u32 shares found */
#define SHA_V_BEST_ZBITS 0x030u   /* u32 leading zero bits of the best hash */
#define SHA_V_BEST_NONCE 0x034u
#define SHA_V_BEST_TIME 0x038u /* ntime of the best hash (time + roll) */
#define SHA_V_LAST_NONCE 0x03cu /* the last share */
#define SHA_V_LAST_TIME 0x040u
#define SHA_V_VERSION 0x044u /* header version (copy) */
#define SHA_V_TIME 0x048u    /* header time (copy) */
#define SHA_V_NBITS 0x04cu   /* header bits (copy) */
#define SHA_V_BEST_HI 0x050u /* the best hash's top 32 bits (display order) */
#define SHA_V_SYSTEM 0x054u  /* system at the last chunk */
#define SHA_V_HEADERS 0x058u /* header changes */
#define SHA_V_LAST_ZBITS 0x05cu
#define SHA_V_BEST 0x060u   /* [u8; 32] best hash, internal order (all 0xff: none) */
#define SHA_V_HEADER 0x080u /* [u8; 80] */
#define SHA_V_TARGET 0x0d0u /* [u8; 32] internal order */
#define SHA_V_MID 0x0f0u    /* [u32; 8] midstate of the header's first block */
#define SHA_V_STAGE 0x110u  /* [u8; 112] request staging (header, target) */
#define SHA_V_RING 0x200u   /* RING x RING_BYTES: the last shares */
#define SHA_RING 32u
#define SHA_RING_BYTES 48u /* nonce, ntime, seq (1-based share number), zbits, hash [u8; 32] */
/* u64: the job item base meshd writes (manifest job_base): OP_JOB grinds chunk V_JOB_BASE + arg. */
#define SHA_V_JOB_BASE 0x1000u
/* The items' result records (manifest job_out): JOB_REC bytes at V_JOB_OUT + JOB_REC (ticket % 64):
 * shares u32, then JOB_SHARES x (nonce u32, zbits u32), then the chunk's ntime u32. */
#define SHA_V_JOB_OUT 0x1800u
#define SHA_JOB_REC 32u
#define SHA_JOB_SHARES 3u
#define SHA_VIEW_MIN 0x2000u
#define SHA_MAGIC 0x36353253u /* "S256" */

/* Nonces per lane per hot-loop iteration. */
#define SHA_ILP 2u
#define SHA_CHUNK_LOG2_MIN 6u /* 5 + trailing_zeros(ILP) */
#define SHA_CHUNK_LOG2_MAX 24u
#define SHA_ZBITS_DEFAULT 26u

#define SHA_OP_ZBITS 3u
#define SHA_OP_RESET 4u
#define SHA_OP_NBITS 5u
#define SHA_OP_GENESIS 6u
/* Refused (R_BAD_OP): the chunk size is the manifest's param. */
#define SHA_OP_CHUNK 7u
#define SHA_OP_BASE 8u
#define SHA_OP_TIME 9u

#endif
