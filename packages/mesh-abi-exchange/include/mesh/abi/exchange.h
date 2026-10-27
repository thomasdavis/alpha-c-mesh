/* @ajax/mesh-abi-exchange: The exchange page of a lockstep group's rounds.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_EXCHANGE_H
#define MESH_ABI_EXCHANGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- exchange */
/* N machines (H_SYSTEMS, up to MAX_SYSTEMS) training as one, owned by the one
 * program with peer = lockstep (`Ctx::exchange`).  Each system writes only its
 * own words; meshd's peer relay (TCP, a full mesh) mirrors system k's slot
 * region and counter to every other machine at the same offsets.
 * ex_counter(k): u64, system k's round counter (= its step + 1).  The page is
 * meshd's: it zeroes it while the lockstep program is halted (load,
 * reset, a view reset, unload), and each peer HELLO zeroes its copy
 * of the peer's region; the program never clears it
 * ex_slots(k):   system k's two slots (parity 0, 1), EX_SLOT_BYTES each:
 * { loss_sum, g0, g1, g2, g3, 0, 0, 0 } (f32 x 8)
 * Round n (u64, never wraps): write slot[n % 2][self], fence.sys, counter[self] = n
 * (release); wait (bounded in all: PEER_POLLS x PEER_POLL_NS) for counter[j] >= n
 * of every other system j, fence, read slot[n % 2][j] of all N; apply their sum
 * folded in system order ((s0 + s1) + s2) + ... (bitwise-identical updates on
 * every machine).  A timed-out round is never skipped: the program keeps it
 * pending and the next task waits for the same round again. */
#define MESH_EX_BASE 0x2A000
#define MESH_EX_BYTES 0x1000
#define MESH_EX_SLOT_BYTES 0x20
#define MESH_EX_REGION_BYTES 0x40
#define MESH_PEER_POLLS 50000u
#define MESH_PEER_POLL_NS 2000u

/* System k's round counter (u64) and its two slots on the exchange page. */
static inline size_t mesh_ex_counter(uint32_t k) { return MESH_EX_BASE + 8 * (size_t)k; }
static inline size_t mesh_ex_slots(uint32_t k) { return MESH_EX_BASE + 0x100 + 0x40 * (size_t)k; }

#endif
