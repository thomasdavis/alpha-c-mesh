/* @ajax/mesh-abi-peer-frames: Peer link frame types (F_*), the link version and port, and liveness timing.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PEER_FRAMES_H
#define MESH_ABI_PEER_FRAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_PEER_PORT 7701
/* ---------------------------------------------------------------- peer frames v6 */
/* The peer link's version, in F_JOIN and F_HELLO.  3 added F_PLACE, F_FRAME,
 * F_INPUT (placement); 4 is the N-machine cluster (F_JOIN, the cluster size in
 * F_HELLO, F_PLACE's assignment); 5 added F_LOADREP; 6 is the fleet (plan F3):
 * partial membership, a leader, the desired state (F_BEAT, F_VOTE_REQ,
 * F_VOTE, F_STATE, F_STATE_ACK, F_COMMIT, F_CTL, F_CTL_REPLY, F_IMAGE_REQ,
 * F_IMAGE) and lockstep groups (F_HELLO, F_ROUND per group); F_RUN, F_RESET,
 * F_TASK, F_LOAD, F_LOADED and F_PING are gone; 7 was the scheduler (plan F4:
 * F_PERF, F_VIEW_REQ, F_VIEW, 18..20) or the jobs (plan F5: F_JOB_AVAIL ..
 * F_RESULT, 21..28) alone; 8 is both; 9 carries a move's outcome in F_BEAT;
 * 10 adds payload results (F_BLOB_PUT .. F_BLOB, 29..32; a result's blob
 * reference in F_RESULT and the job log); 11 names the job in F_BLOB_ACK (blobs are
 * referenced per job); 12 adds a node's limits to F_JOIN (max_threads, max_shared:
 * multi-warp nodes).  A peer of another version is refused at JOIN. */
#define MESH_PEER_VERSION 12u
/* The fleet: N systems (2..=MAX_SYSTEMS), a full mesh of TCP links.  Every
 * system k > 0 listens on its peer address; system k dials every system j > k.
 * Each link starts with F_JOIN both ways (ids, version, size, capabilities)
 * and lives on its own: a member is *up* while its link is (every member
 * beats every other at PEER_PING_MS; a link silent for PEER_DEAD_MS closes).
 * Members elect a leader with a majority of N (mesh-host fleet.rs; N = 2: system
 * 0 always), the leader owns the desired state (the document) and commits
 * every change on a majority, and each member reconciles its programs to the
 * committed document.  Lockstep groups exchange point to point among their
 * members (F_HELLO, F_ROUND); placement frames go point to point too.
 * u8 type then the body:
 * F_HELLO  (1): u32 system, u32 PEER_VERSION, [u8; 16] group, u32 epoch, u32 cycle,
 * u32 member mask, [u8; 8] rank order (system ids, 0xff past the end),
 * u64 image hash -- a group member, ready for (epoch, cycle) of its group,
 * to every other member; the members run once every one's HELLO matches
 * F_ROUND  (2): u32 epoch, u32 cycle, u64 counter, EX_REGION_BYTES -- the sender's
 * ex_counter(rank) and ex_slots(rank); the receiver drops a round whose
 * (epoch, cycle) is not its own, and writes it at the sender's rank
 * F_BEAT   (3): u64 term, u32 flags (BEAT_LEADER), u32 leader (PEER_NONE), u64 commit_term,
 * u64 commit_version, u64 accepted_term, u64 accepted_version, u64 applied
 * version, u32 RS_* reconcile status, u32 up mask (whom it hears), u32 group
 * epoch, u32 GL_* group state, u32 reason_len (<= BEAT_REASON_MAX), reason,
 * u32 moves (<= BEAT_MOVES_MAX), then per program a move placed here (a
 * migration or a restart): [u8; 16] name, u64 version, u32 ok, u32 gap_us
 * (BEAT_MOVE_BYTES each; the leader's verdict on that move) --
 * every member to every member each PEER_PING_MS (liveness, the leader's
 * authority, catch-up, and the status every `fleet` shows)
 * F_VOTE_REQ (4): u32 pre (1: a pre-vote), u64 term, u64 accepted_term, u64 accepted_version
 * F_VOTE   (5): u32 pre, u64 term, u32 granted
 * F_STATE  (6): u64 term, u32 len (<= F_STATE_MAX), the document (text) -- the leader's
 * F_STATE_ACK (7): u64 term, u64 doc_term, u64 doc_version -- persisted
 * F_COMMIT (8): u64 term, u64 doc_term, u64 doc_version -- committed on a majority
 * F_PLACE  (9): u32 PL_*, u32 id, [u8; 16] name, u32 PLACE_*, u32 sys, [u8; PLACE_PARTS_MAX]
 * assignment (split: part b's system; else PART_NONE), u32 aux (PL_PAUSED: the
 * sender's mean ns per part, 0 unknown; PL_SET: the member mask of the
 * change in bits 0..8 and the system whose state continues in bits 8..16), u32 len, then len bytes (<= F_PLACE_MAX): the state regions
 * (PL_PAUSED, PL_SET) or a reason (PL_FAIL).  A placement change, coordinated
 * by the leader among the up members
 * F_FRAME (10): [u8; 16] name, u32 frame, u32 parts (bit b: part b is in data), u32 sys
 * (the sender), u32 state_len, u32 data_len, then the state regions and,
 * per part in ascending order, that part of each place_out region
 * (<= F_PLACE_MAX in all)
 * F_INPUT (11): [u8; 16] name, u32 len (<= F_INPUT_MAX), bytes -- input forwarded to the
 * machine that applies it (pin k: k; split: the owner)
 * F_JOIN  (12): u32 system, u32 PEER_VERSION, u32 systems, u32 cc (major * 10 + minor),
 * u32 sms, u32 sms_total, u32 blocks_per_sm, u32 backend (H_BACKEND; 0 CUDA), [u8; 32] gpu name,
 * [u8; 16] host arch, [u8; 32] hostname, u32 max_threads, u32 max_shared (a node's
 * limits: H_MAX_TG_THREADS, H_MAX_SHARED; 0 without a GPU) -- the first frame each way on a
 * link; a version or size mismatch, or an unexpected system, closes it
 * F_LOADREP (13): LOADREP_BYTES: the sender's load summary (its member load record
 * from ML_SHARE on), after each load sample (~1 s), to every linked member;
 * the receiver stores it at mload(sender) with its own receive time
 * F_CTL   (14): u32 id, u32 len (<= F_CTL_MAX), a control request line -- a change asked
 * for on a member that does not lead, forwarded to the leader
 * F_CTL_REPLY (15): u32 id, u32 ok, u32 len (<= F_CTL_REPLY_MAX), the reply text
 * F_IMAGE_REQ (16): u32 id, [u8; 16] name, u64 hash, u32 dir_len (<= 64), dir -- a member
 * reconciling asks for an image its catalog does not have at that hash
 * F_IMAGE (17): u32 id, u64 hash, u32 ok, u32 dir_len, u32 manifest_len, u32 code_len,
 * then the dir, the manifest and the PTX (ok), or the reason in the manifest
 * field (not ok); <= F_LOAD_MAX in all
 * F_PERF  (18): u32 len (<= F_PERF_MAX), text -- the sender's scheduler report after each
 * load sample (~1 s), to every linked member: `caps ...` (CPUs, RAM), `prog
 * <name> hash .. exec .. task_us ..` per loaded program, `rtt <k> <us>` per
 * member it measured, `echo <k> <t_us> <held_us>` per member it heard (link
 * RTT: the receiver of an echo of its own send time t computes now - t - held)
 * F_VIEW_REQ (19): u32 id, [u8; 16] name, u64 hash -- a migration's target asks the
 * source (the desired state names both) for the program's view; the source
 * stops it and answers with F_VIEW
 * F_VIEW  (20): u32 id, u32 ok, u32 view_abi, u32 total, u32 off, u32 len, then len bytes
 * (<= F_VIEW_CHUNK): the view in chunks (ok), or the reason (not ok)
 * Jobs (plan F5; mesh-host jobs.rs has the bodies): u8 type, u32 len (<= F_JOB_MAX),
 * then the body, which starts with the u64 term of the leader it is from or for:
 * F_JOB_AVAIL (21): member -> leader: the job programs running here (name, executor,
 * tickets in flight) and the leases it holds (for a new leader to adopt)
 * F_JOB_STATE (22): leader -> member: the replicated job table: a delta (ops) of seq n,
 * a snapshot or a term's first entry in numbered parts (the end says how
 * many: a part lost is never taken for a whole table), or the commit point
 * F_JOB_ACK   (23): member -> leader: entries up to n persisted (or: a gap, send a snapshot)
 * F_JOB_PULL  (24): a new leader -> member: send your whole table
 * F_JOB_HAVE  (25): member -> the new leader: its committed table (with its commit point)
 * and its entries not committed yet (each with its leadership and number),
 * in numbered parts, then how many and its leases
 * F_LEASE     (26): leader -> member: run these items (job, attempt, program, op, setup, ranges)
 * F_LEASE_CUT (27): leader -> member: drop the lease's items from `cut` on (0: all)
 * F_RESULT    (28): member -> leader: item results (ok, failed with a reason, returned unrun);
 * a payload item's result names its blob (sha256, length, holders)
 * Blobs (an item's payload, off the job log; mesh-host blobd.rs), u8 type, u32 len
 * (<= F_BLOB_MAX), then the body (a 32-byte sha256 first).  PUT and BLOB carry bytes
 * and go through the link's bulk queue, behind its control frames (Shared::send_bulk):
 * F_BLOB_PUT  (29): hash, u64 job, u32 len, the bytes -- keep this blob (durably) for that job
 * F_BLOB_ACK  (30): hash, u32 ok, u64 job -- that job's copy stored and synced (ok), or refused (full)
 * F_BLOB_GET  (31): hash, u64 job -- send me this blob
 * F_BLOB      (32): hash, u32 found, the bytes (found) -- the answer */
#define MESH_F_HELLO 1
#define MESH_F_ROUND 2
#define MESH_F_BEAT 3
#define MESH_F_VOTE_REQ 4
#define MESH_F_VOTE 5
#define MESH_F_STATE 6
#define MESH_F_STATE_ACK 7
#define MESH_F_COMMIT 8
#define MESH_F_PLACE 9
#define MESH_F_FRAME 10
#define MESH_F_INPUT 11
#define MESH_F_JOIN 12
#define MESH_F_LOADREP 13
#define MESH_F_CTL 14
#define MESH_F_CTL_REPLY 15
#define MESH_F_IMAGE_REQ 16
#define MESH_F_IMAGE 17
#define MESH_F_PERF 18
#define MESH_F_VIEW_REQ 19
#define MESH_F_VIEW 20
#define MESH_F_JOB_AVAIL 21
#define MESH_F_JOB_STATE 22
#define MESH_F_JOB_ACK 23
#define MESH_F_JOB_PULL 24
#define MESH_F_JOB_HAVE 25
#define MESH_F_LEASE 26
#define MESH_F_LEASE_CUT 27
#define MESH_F_RESULT 28
#define MESH_F_BLOB_PUT 29
#define MESH_F_BLOB_ACK 30
#define MESH_F_BLOB_GET 31
#define MESH_F_BLOB 32
/* Every peer frame type, in id order: the guard below holds them unique and
 * dense (1..=32), so a branch that adds a frame cannot reuse another's id. */
#define MESH_FRAMES_LEN 32
extern const uint8_t MESH_FRAMES[32];
/* Liveness (defaults; a cluster config's `ping_ms` / `dead_ms` override them):
 * each member beats every other every PEER_PING_MS; a link that received
 * nothing for PEER_DEAD_MS closes (a peer that vanished without a FIN: power
 * loss, a SIGSTOP, a pulled cable), and a member silent for a third of it is
 * *suspect* (shown, still up). */
#define MESH_PEER_PING_MS UINT64_C(250)
#define MESH_PEER_DEAD_MS UINT64_C(3000)

#endif
