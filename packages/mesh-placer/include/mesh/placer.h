/* @ajax/mesh-placer: one placed program's state machine (place.rs Placer), sans-IO. The driver feeds
 * it events (a frame batch completed here, a peer frame arrived, a control request, the fleet's view,
 * a tick) and carries out the acts it returns (send a frame, write the state into the view, show a
 * composed frame, publish the follow batch, change the run state, answer a request).
 *
 * The fleet's leader coordinates every change among the up members: it pauses the others (PL_PAUSE),
 * collects their drained state and time per part (PL_PAUSED), picks the state that continues, sets the
 * new mode everywhere (PL_SET with that state) and waits for PL_DONE; a change not done in 5 s is
 * rolled back (PL_FAIL). It re-plans when a member of the mode drops, and 5 s after one comes back.
 * Frames go point to point. Nothing here knows doom: the manifest's regions say what moves. Times are
 * monotonic microseconds. */
#ifndef MESH_PLACER_H
#define MESH_PLACER_H

#include "mesh/place_compose.h"
#include "mesh/place_mode.h"
#include "mesh/place_spec.h"
#include "mesh/place_wire.h"
#include "mesh/mt.h"

#include <stdbool.h>
#include <stdint.h>

#define MPL_CHANGE_TIMEOUT_US 5000000ull /* a change that has not finished by then is rolled back */
#define MPL_JOIN_DWELL_US 5000000ull     /* after a member comes back, the leader waits this long */

/* Where a frame goes: one member, or every member in a mask. */
typedef struct {
    bool to_mask;
    uint32_t k; /* to, or the mask */
} mpl_dest;

typedef enum {
    MPL_SEND,        /* dest, bytes: a whole peer frame (type byte first) */
    MPL_WRITE_STATE, /* bytes: the state regions into the view (only while nothing is in flight) */
    MPL_SHOW,        /* frame, sys (SHB_SYS), bytes (the buffer), lat_us: a whole frame into the show region */
    MPL_FOLLOW,      /* frame: publish the follow batch (OP_FOLLOW, this system's parts) */
    MPL_MODE,        /* mode: the mode changed here (run state, PX_PLACE, SH_MODE) */
    MPL_ANSWER,      /* handle, ok, text: answer a local request */
    MPL_LOG,         /* text */
} mpl_act_kind;

typedef struct {
    mpl_act_kind kind;
    mpl_dest dest;
    mpw_bytes bytes;
    uint32_t frame, sys, lat_us;
    mpm_mode mode;
    uint64_t handle;
    bool ok;
    char *text;
} mpl_act;

typedef struct {
    mpl_act *v;
    size_t n, cap;
} mpl_acts;

void mpl_acts_free(mpl_acts *a);

typedef struct {
    uint64_t shown, lat_n, lat_sum_us;
    uint32_t lat_max_us;
    uint64_t ignored; /* peer frames this role had no use for */
} mpl_stats;

typedef struct mpl_placer mpl_placer;

/* Takes spec. log: an optional line logger (the Rust's info! lines that are not acts). */
mpl_placer *mpl_new(const char *name, mps_spec spec, uint32_t me, uint32_t n);
void mpl_free(mpl_placer *p);
void mpl_set_log(mpl_placer *p, void (*log)(void *ctx, const char *line), void *ctx);

const mpm_mode *mpl_mode(const mpl_placer *p);
mpm_role mpl_role(const mpl_placer *p);
bool mpl_paused(const mpl_placer *p);
bool mpl_busy(const mpl_placer *p);
uint32_t mpl_members(const mpl_placer *p);
uint32_t mpl_my_parts(const mpl_placer *p);
const mpl_stats *mpl_stats_of(const mpl_placer *p);
void mpl_set_part_ns(mpl_placer *p, uint32_t ns); /* this machine's mean time per part, for split auto */
const mps_spec *mpl_spec(const mpl_placer *p);

/* A control request's words, as the cluster's size allows them. */
bool mpl_parse(const mpl_placer *p, char *const *words, size_t n, mpm_target *out, char **err);
/* The target of a request over the members that are up now. */
bool mpl_plan(const mpl_placer *p, const mt_strs *words, mpm_target *out, char **err);

/* The leader's request (the committed placement request): plan it over the up members, run the change. */
mpl_acts mpl_request(mpl_placer *p, const mt_strs *words, uint64_t handle, uint64_t now);
/* The fleet's view changed: the up members, the leader (has_coord), the committed request (NULL: local). */
mpl_acts mpl_view(mpl_placer *p, uint32_t up, bool has_coord, uint32_t coord, const mt_strs *request, uint64_t now);
/* Per driver tick: drained (nothing of the program in flight here) and this machine's state regions,
 * read on demand. */
typedef mpw_bytes (*mpl_state_fn)(void *ctx);
mpl_acts mpl_tick(mpl_placer *p, uint64_t now, bool drained, mpl_state_fn state, void *ctx);
/* A peer F_PLACE from system from (takes m). */
mpl_acts mpl_peer_place(mpl_placer *p, mpw_place m, uint32_t from, uint64_t now);
/* A whole batch of this machine's own frame finished (Runner: every part; Owner: its parts). Takes the bytes. */
mpl_acts mpl_own_frame(mpl_placer *p, uint32_t frame, mpw_bytes state, mpw_bytes data, uint64_t now);
/* The follow batch (Follower) finished: its parts' data (taken). */
mpl_acts mpl_follow_done(mpl_placer *p, mpw_bytes data, uint64_t now);
/* The follow batch was lost (the kernel halted under it). */
void mpl_follow_lost(mpl_placer *p);
/* A peer F_FRAME (takes m). */
mpl_acts mpl_peer_frame(mpl_placer *p, mpw_frame m, uint64_t now);
/* Unloaded / its manifest's placement changed: answer what waits. */
mpl_acts mpl_drop_all(mpl_placer *p, const char *why);

#endif
