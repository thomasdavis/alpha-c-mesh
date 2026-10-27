/* @ajax/mesh-lease: the lease (mesh-host lease.rs): mesh-rt's node loop played by a host thread
 * against the mailbox, for executors without resident kernels -- wasm (CPU programs) and, in the
 * Rust, Metal. One maintained owner of the node-loop protocol; the executor supplies only a runner
 * that runs the taken tickets (wasm: calls into an instance).
 *
 * A lease lane owns `count` node records from `first` (wasm: one lane per node):
 * - start: writes its node records (N_SM = the node number), P_ONLINE += count;
 * - each turn it takes published tickets in order -- E_SEQ == t + 1, at most the runner's
 *   max_take, none while H_BLINK is set if the lane is gated by it (a wasm lane is never relaunched
 *   by a GPU blink and ignores it) -- each by a CAS on P_RING_HEAD, so several lanes of one slot
 *   share its ring without a lock and no lane ever holds an unpublished ticket;
 * - the runner runs them and completes each exactly as mesh-rt's nodes do (mesh_lease_complete):
 *   P_RESULTS, P_MARKS = t + 1 (release), P_DONE += 1, the node record (tasks, last op, busy ns,
 *   heartbeat), a trace record;
 * - a fault (mesh_lease_fault, the first kept) leaves the tickets incomplete: the driver fails the
 *   slot and shuts it down;
 * - P_SHUTDOWN with nothing published: P_HALTED += count and the lane ends.
 *
 * mesh_round is the lockstep round (train), with the bounded wait in the host lane: publish
 * ex_counter(k) = n, wait PEER_POLLS x PEER_POLL_NS for every member's counter >= n.
 *
 * A gated (GPU) lease counts each run in the slot's pipe flight word (pipe flight_begin/end); a
 * wasm lane's pushes count themselves (host calls). */
#ifndef MESH_LEASE_H
#define MESH_LEASE_H

#include "mesh/abi/control_block.h"
#include "mesh/mailbox.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A lockstep round's outcome. */
#define MESH_ROUND_OK 1u
#define MESH_ROUND_TIMEOUT 2u

/* Idle lanes heartbeat their node records this often (mesh-rt: HEARTBEAT_NS). */
#define MESH_LEASE_HEARTBEAT_NS 2000000ull

/* Host clock in ns (CLOCK_MONOTONIC): the trace's and the node records' timebase on a lease. */
uint64_t mesh_host_ns(void);

/* A taken ticket: (ticket, op, arg, completion slot). */
typedef struct {
    uint32_t t, op, arg, cslot;
} mesh_taken;

/* Counters for tools: batches run, tasks, passes, lockstep rounds and the wait in them. */
typedef struct {
    _Atomic uint64_t buffers, tasks, passes, rounds, round_wait_ns;
} mesh_lane_stats;

typedef struct mesh_lease mesh_lease;

/* What an executor does with taken tickets. */
typedef struct {
    /* At most this many tickets per turn. */
    size_t (*max_take)(void *self);
    /* Runs taken[0..n) and completes each (mesh_lease_complete); false: a fault was recorded
     * (mesh_lease_fault) and the rest stay incomplete. */
    bool (*run)(void *self, mesh_lease *lease, const mesh_taken *taken, size_t n);
    /* Idle, at each heartbeat (wasm: its CPU time and memory into cpuuse). NULL: nothing. */
    void (*heartbeat)(void *self);
} mesh_runner;

/* One lane of a running slot. */
struct mesh_lease {
    mesh_mailbox mb;
    uint32_t slot;
    uint32_t first, count; /* the node records this lane owns: first .. first + count */
    bool blink_gate;       /* no ticket is taken while H_BLINK is set (GPU leases) */
    _Atomic bool *done;    /* set when the lane ends (the caller's, outlives the lane) */
    mesh_lane_stats *stats; /* the caller's */
    uint64_t beat_at;
};

/* done and stats are the caller's and must outlive the lane (the Rust's Arcs). */
void mesh_lease_init(mesh_lease *l, mesh_mailbox mb, uint32_t slot, uint32_t first, uint32_t count, bool blink_gate,
                     _Atomic bool *done, mesh_lane_stats *stats);
static inline size_t mesh_lease_ctl(const mesh_lease *l, size_t off) { return mesh_prog_ctl(l->slot) + off; }

/* The node loop. Runs on its own thread until P_SHUTDOWN with nothing published. */
void mesh_lease_run(mesh_lease *l, const mesh_runner *runner, void *self);

/* mesh-rt's completion of ticket k on node: result, mark (release), P_DONE, the node record, the
 * trace (start/end: host or GPU ns). */
void mesh_lease_complete(mesh_lease *l, uint32_t node, const mesh_taken *k, uint32_t result, uint64_t start, uint64_t end);
/* Records the slot's first fault (P_FAULT keeps only the first). */
void mesh_lease_fault(const mesh_lease *l, uint32_t code);
/* The lockstep round n (mesh_round), counted in the lane's stats. */
uint32_t mesh_lease_exchange(mesh_lease *l, uint64_t n);

/* Round n for group rank k of N (H_GROUP_RANK, H_GROUP_N): publish ex_counter(k) = n (unless
 * already), wait bounded for every other member's counter >= n. MESH_ROUND_OK or
 * MESH_ROUND_TIMEOUT, and the time waited in *waited_ns (may be NULL). Not in a group:
 * MESH_ROUND_TIMEOUT at once. */
uint32_t mesh_round(mesh_mailbox mb, uint64_t n, uint64_t *waited_ns);

#endif
