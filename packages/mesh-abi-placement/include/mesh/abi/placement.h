/* @ajax/mesh-abi-placement: Placement across machines: capabilities, modes, split parts and the show region (SH_*).
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_PLACEMENT_H
#define MESH_ABI_PLACEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- placement */
/* Where a program runs on a cluster of N machines (control `place <name>
 * local|pin <k>|split [spec]`).  Only a program whose manifest declares `place`
 * can be placed; meshd moves its state and frames, generically:
 * place_state  view regions of the shared state (doom: pose, frame, map)
 * place_frame  the u32 frame number (inside a state region)
 * place_out    view regions a frame writes; task arg b writes the b-th of
 * `fanout` equal parts of each (doom: 4 x 32 columns)
 * place_show   SH_* below: meshd's frame-matched copy of the output
 * - local: each machine runs its own copy (the default; nothing mirrored).
 * - pin k: only system k runs it; every other machine's kernel is stopped, its
 * view gets k's state and whole frames (F_FRAME) and its input is forwarded
 * to k (F_INPUT).
 * - split: an *assignment* maps each part b (0..fanout) to a system.  The
 * owner, the system of part 0, owns the state and runs its parts with
 * OP_TASK (part 0 applies the input); every other assigned system (a
 * follower) runs its parts with OP_FOLLOW on the state the owner sent with
 * that frame.  Each sends its parts to every other machine; systems without
 * parts mirror.  Every machine shows a frame only once it holds every part of
 * the SAME frame number.  Input goes to the owner.  Specs: `split` (every
 * system, parts dealt in order, earlier systems take the remainder: N = 3,
 * fanout 4 -> 0 0 1 2), `split a,b,..` (those systems, the same way; the
 * first owns), `split a:n,b:m,..` (n parts to a, ...; they sum to fanout),
 * a list of fanout systems (one per part, repeats allowed: `split 0,0,1,2`),
 * or `split auto` (every system, parts weighted by each machine's measured
 * time per part, the fastest owning: a slower GPU gets fewer columns).
 *
 * A change pauses every copy, drains them, moves the state from the machine
 * whose state continues (pin k: k; split: its owner; from local: the new
 * runner/owner) and resumes; the fleet's leader coordinates it among the up
 * members, re-plans when one of them drops (the state from a survivor's copy)
 * and when one returns; without a leader, a member that lost a placement peer goes local. */
#define MESH_PLACE_CAP_NONE 0u
#define MESH_PLACE_CAP_PIN 1u
#define MESH_PLACE_CAP_SPLIT 2u
#define MESH_PLACE_CAPS_LEN 3
extern const char *const MESH_PLACE_CAPS[3];
#define MESH_PLACE_LOCAL 0u
#define MESH_PLACE_PIN 1u
#define MESH_PLACE_SPLIT 2u
#define MESH_PLACES_LEN 3
extern const char *const MESH_PLACES[3];
/* Parts a split can assign (task args 0..fanout; part masks are one u32). */
#define MESH_PLACE_PARTS_MAX 32
/* PX_PLACE_ASSIGN's entry for a part beyond fanout (and every part when not split). */
#define MESH_PART_NONE 0xff
/* The show region (manifest `place_show`, view offset S), written by meshd
 * in pin and split: two buffers, the output regions concatenated in each.
 * S + SH_SEQ     u32 frames shown; the current buffer is SH_SEQ & 1
 * (its data, then its SHB_*, then SH_SEQ: release)
 * S + SH_MODE    u32 PLACE_* (local: readers use the program's own output)
 * S + SH_SYS     u32 the pinned system / the split owner
 * S + SH_LAT_US  u32 split: the last frame's added latency, its first part
 * here to its last (us); pin: 0
 * S + SH_BUF + SH_BUF_BYTES k: buffer k's SHB_FRAME (frame number) and
 * SHB_SYS (4 bits per part b < 8: the system that rendered it)
 * S + SH_DATA + k * out_bytes: buffer k's data */
#define MESH_SH_SEQ 0x0
#define MESH_SH_MODE 0x4
#define MESH_SH_SYS 0x8
#define MESH_SH_LAT_US 0xc
#define MESH_SH_BUF 0x10
#define MESH_SH_BUF_BYTES 0x10
#define MESH_SHB_FRAME 0x0
#define MESH_SHB_SYS 0x4
#define MESH_SH_DATA 0x100

/* The show region's bytes for out_bytes of output. */
static inline uint32_t mesh_show_bytes(uint32_t out_bytes) { return (uint32_t)MESH_SH_DATA + 2 * out_bytes; }

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_SH_BUF + MESH_SH_BUF_BYTES * 2 <= MESH_SH_DATA, "SH_BUF + SH_BUF_BYTES * 2 <= SH_DATA");
_Static_assert(MESH_SHB_SYS + 4 <= MESH_SH_BUF_BYTES, "SHB_SYS + 4 <= SH_BUF_BYTES");
_Static_assert(MESH_PLACE_PARTS_MAX == 32, "PLACE_PARTS_MAX == 32");

#endif
