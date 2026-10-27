/* @ajax/mesh-fleet-core: the election and commit core (fleet.rs Core), sans-IO.
 *
 * Election (Raft's rules, no log). The replicated state is one small document, sent whole, so the
 * log is a single (term, version) pair.
 * - Each member persists its term, its vote and two documents: the last it accepted from a leader
 *   and the last it knows committed (mesh-fleet-persist).
 * - A follower that hears no leader for dead_ms (or whose link to the leader closed) waits
 *   150 ms x its id + 0..100 ms, then asks for pre-votes (Raft's PreVote): a member grants one only
 *   if it does not hear a leader itself and the candidate's accepted (term, version) is at least its
 *   own. A majority of pre-votes starts a real election: term + 1, one vote per term (persisted
 *   before it is sent), the same up-to-date rule. A majority of the configured N makes a leader.
 * - A leader that does not hear a majority within dead_ms steps down (CheckQuorum).
 * - A change: the leader builds the next document from its committed one (version + 1, its term),
 *   persists it, sends it to every linked member (State), commits once a majority persisted it
 *   (StateAck), and tells everyone (Commit). One change is in flight at a time. A member that was
 *   away gets the document when its beat shows it behind.
 * - N = 2 has no majority after a loss, so a pair keeps a fixed coordinator: system 0 leads (term 1)
 *   and commits alone; system 1 never leads.
 *
 * The shell feeds it link changes, ticks and received messages, and carries out the outputs in
 * order: every Persist is written before anything after it is sent (mfc_durable). */
#ifndef MESH_FLEET_CORE_H
#define MESH_FLEET_CORE_H

#include "mesh/abi/base.h"
#include "mesh/fleet_doc.h"
#include "mesh/fleet_persist.h"

#include <stdbool.h>
#include <stdint.h>

/* A document id: (term, version). */
typedef struct {
    uint64_t term, version;
} mfc_id;

static inline int mfc_id_cmp(mfc_id a, mfc_id b) { return mfd_id_cmp(a.term, a.version, b.term, b.version); }
static inline mfc_id mfc_doc_id(const mfd_doc *d) { return (mfc_id){d->term, d->version}; }

typedef enum { MFC_BEAT, MFC_VOTE_REQ, MFC_VOTE, MFC_STATE, MFC_STATE_ACK, MFC_COMMIT } mfc_msg_kind;

/* The core's wire messages (the peer link encodes them; `from` is the link's member). */
typedef struct {
    mfc_msg_kind kind;
    uint64_t term;
    bool leader;        /* Beat */
    mfc_id commit, acc; /* Beat: committed and accepted; VoteReq: acc */
    bool pre;           /* VoteReq, Vote */
    bool granted;       /* Vote */
    mfd_doc doc;        /* State */
    mfc_id ver;         /* StateAck, Commit */
} mfc_msg;

mfc_msg mfc_msg_clone(const mfc_msg *m);
void mfc_msg_free(mfc_msg *m);

typedef enum { MFC_FOLLOWER, MFC_PRE_CANDIDATE, MFC_CANDIDATE, MFC_LEADER } mfc_role;
const char *mfc_role_word(mfc_role r);
uint32_t mfc_role_code(mfc_role r);

typedef struct {
    uint32_t me, n;
    uint64_t dead_ms; /* a member silent this long is gone; the election timeout's base */
    uint64_t seed;
} mfc_config;

uint32_t mfc_quorum(const mfc_config *c); /* a majority of n; 1 for the fixed pair */
bool mfc_fixed(const mfc_config *c);      /* n == 2 */

typedef enum { MFC_OUT_PERSIST, MFC_OUT_SEND, MFC_OUT_COMMITTED, MFC_OUT_LEADER, MFC_OUT_DONE, MFC_OUT_LOG } mfc_out_kind;

/* What the shell must do after an input, in order. */
typedef struct {
    mfc_out_kind kind;
    uint32_t to;       /* Send */
    mfc_msg msg;       /* Send */
    bool has_leader;   /* Leader */
    uint32_t leader;
    uint64_t version;  /* Done */
    bool ok;           /* Done */
    char *text;        /* Done's error, Log's line */
} mfc_out;

typedef struct {
    mfc_out *v;
    size_t n, cap;
} mfc_outs;

void mfc_outs_free(mfc_outs *o);

/* A core's outputs made durable, in order: every Persist is written (by persist) before the outputs
 * after it are carried out. The first write that fails stops it (fail-stop): out keeps the outputs
 * before it, and false is returned with *err; nothing after it -- no vote, acknowledgement or commit
 * the disk does not hold -- ever comes out. Persist entries are removed. Takes outs. */
typedef bool (*mfc_persist_fn)(void *ctx, char **err);
bool mfc_durable(mfc_outs *outs, mfc_persist_fn persist, void *ctx, char **err);

typedef struct mfc_core mfc_core;

mfc_core *mfc_new(mfc_config cfg, mfp_persist p, uint64_t now); /* takes p */
void mfc_free(mfc_core *c);

const mfc_config *mfc_cfg(const mfc_core *c);
const mfp_persist *mfc_persist(const mfc_core *c);
uint64_t mfc_term(const mfc_core *c);
mfc_role mfc_role_of(const mfc_core *c);
bool mfc_leader(const mfc_core *c, uint32_t *leader);
const mfd_doc *mfc_committed(const mfc_core *c);
const mfd_doc *mfc_accepted(const mfc_core *c);
bool mfc_busy(const mfc_core *c);
/* This leadership committed a document of its own term (until then committed may be behind). */
bool mfc_term_committed(const mfc_core *c);
uint64_t mfc_next_version(const mfc_core *c);
/* The leader's first document of its term: the accepted one; false for a fresh fleet. */
bool mfc_first_doc(const mfc_core *c, mfd_doc *out);
uint32_t mfc_linked(const mfc_core *c);
bool mfc_voted(const mfc_core *c, uint32_t *voted);
/* Members heard from within dead_ms (this one included). */
uint32_t mfc_alive(const mfc_core *c, uint64_t now);
bool mfc_has_quorum(const mfc_core *c, uint64_t now);

mfc_outs mfc_link(mfc_core *c, uint32_t k, bool up, uint64_t now);
mfc_outs mfc_tick(mfc_core *c, uint64_t now);
/* Takes m. */
mfc_outs mfc_recv(mfc_core *c, uint32_t from, mfc_msg m, uint64_t now);
/* This member's beat. */
mfc_msg mfc_beat(const mfc_core *c);
/* The leader proposes doc (its version and term are set here; takes doc): true with *version in
 * flight and *outs, or false with *err why not. */
bool mfc_propose(mfc_core *c, mfd_doc doc, uint64_t now, uint64_t *version, mfc_outs *outs, char **err);

#endif
