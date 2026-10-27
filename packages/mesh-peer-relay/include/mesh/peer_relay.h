/* @ajax/mesh-peer-relay: the fleet thread (peer.rs run / Relay, peer_sched.rs). N machines run as one
 * fleet with partial membership:
 * - Links: a full mesh of TCP links (mesh-peer-link). Every member beats every other each ping_ms; a
 *   member is up while its link is and it was heard within dead_ms, suspect when silent for a third of
 *   that. Nothing ends when a member drops: only the lockstep group that contains it pauses.
 * - The leader and the desired state (mesh-fleet-core): a change asked for on any member goes to the
 *   leader (F_CTL, answered with F_CTL_REPLY once the reachable members reconciled it); without a
 *   leader it is refused. The leader commits it, waits for the members to reconcile it, and reverts a
 *   change that failed everywhere. Each committed document goes to the daemon's reconciler; images and
 *   migrated views are fetched from members for it.
 * - The leader's policies: lockstep groups (pause, resume, re-form) and the scheduler (placement,
 *   rebalancing, migrations, group proposals) from a model of every member (F_JOIN, F_LOADREP, F_PERF).
 * - Lockstep groups: the members HELLO each epoch and cycle; rounds are mirrored point to point.
 * - Published: the status block's fleet regions, the `fleet`, `groups` and `sched` texts and the fleet
 *   view's records.
 * Everything daemon-specific (the status block, the mailbox, the driver, the catalog, the reconciler,
 * jobs and blobs) comes through mpr_host. */
#ifndef MESH_PEER_RELAY_H
#define MESH_PEER_RELAY_H

#include "mesh/cluster_conf.h"
#include "mesh/fleet_edit.h"
#include "mesh/fleet_persist.h"
#include "mesh/fleet_plan.h"
#include "mesh/peer_codec.h"
#include "mesh/place_wire.h"
#include "mesh/sched_report.h"
#include "mesh/sched_tracker.h"

#include <stdbool.h>
#include <stdint.h>

/* S_PEER_STATE */
#define MPR_PEER_WAITING 1u
#define MPR_PEER_UP 2u

/* An answer to a change: ok, and the text (the change's outcome or the reason). */
typedef struct {
    void (*fn)(void *ctx, bool ok, const char *text);
    void *ctx;
} mpr_reply;

/* The reconciler's status as the beats say it. */
typedef struct {
    uint64_t applied;
    uint32_t status;
    char *reason;
    mst_outcome *moves;
    size_t nmoves;
} mpr_recon;

void mpr_recon_free(mpr_recon *r);

/* A program image as members exchange it (F_IMAGE): its catalog dir, manifest text and PTX. */
typedef struct {
    char *dir, *manifest;
    mpw_bytes ptx;
} mpr_payload;

void mpr_payload_free(mpr_payload *p);

typedef struct {
    void *ctx;
    /* The status block. */
    void (*host_u32)(void *ctx, size_t off, uint32_t v);
    void (*host_u64)(void *ctx, size_t off, uint64_t v);
    void (*host_str)(void *ctx, size_t off, size_t cap, const char *s);
    void (*host_bytes)(void *ctx, size_t off, const uint8_t *b, size_t n);
    void (*host_read)(void *ctx, size_t off, uint8_t *out, size_t n);
    uint64_t (*uptime_ms)(void *ctx);
    /* The mailbox: the lockstep exchange regions and the program table. */
    uint64_t (*mb_get64)(void *ctx, size_t off);
    uint32_t (*mb_get32)(void *ctx, size_t off);
    void (*mb_read)(void *ctx, size_t off, uint8_t *out, size_t n);
    void (*mb_write)(void *ctx, size_t off, const uint8_t *b, size_t n);
    void (*mb_publish64)(void *ctx, size_t off, uint64_t v);
    void (*lock_feed)(void *ctx, bool on); /* feed the lockstep program */
    /* A lockstep round received from member k reached counter n: (rtt µs or < 0, wait µs). */
    void (*round_stats)(void *ctx, uint32_t k, uint64_t n, double rtt_us, double wait_us);
    /* The reconciler: a committed document to apply; its status. */
    void (*committed)(void *ctx, const mfd_doc *doc);
    void (*recon)(void *ctx, mpr_recon *out);
    /* The driver and the catalog. */
    bool (*catalog)(void *ctx, const char *dir, mfe_image *out, char **err);
    mfl_actual *(*inventory)(void *ctx, size_t *n);
    /* This member's catalog image of dir if it is name at hash (F_IMAGE_REQ). */
    bool (*image)(void *ctx, const char *dir, const char *name, uint64_t hash, mpr_payload *out, char **err);
    /* Stop a program and read its view (a migration's source; called on its own thread). */
    bool (*stop_and_view)(void *ctx, const char *name, uint32_t *view_abi, mpw_bytes *view, char **err);
    /* The fleet's view for the driver's placement: up members, leader, placement requests. */
    void (*driver_view)(void *ctx, uint32_t up, bool has_leader, uint32_t leader, const mfd_doc *places);
    /* Frames for others: placement (F_PLACE, F_FRAME, F_INPUT: the whole frame, taken), jobs, blobs. */
    void (*place_frame)(void *ctx, uint32_t from, mpw_bytes frame);
    void (*jobs_frame)(void *ctx, uint32_t from, uint8_t type, const uint8_t *body, size_t n);
    void (*blobs_frame)(void *ctx, uint32_t from, uint8_t type, const uint8_t *body, size_t n);
    /* A `job ...` request: local (reply) or from member from (answer with mpr_ctl_reply(id)). */
    void (*jobs_ctl)(void *ctx, const char *line, bool remote, uint32_t from, uint32_t id, mpr_reply reply);
    void (*jobs_view)(void *ctx, uint64_t term, bool has_leader, uint32_t leader, uint32_t alive, uint32_t linked);
    /* The scheduler's model for the jobs dispatcher (jobs::select): the members, the programs' needs
     * (the committed document), the measured performance and the weights. */
    void (*jobs_sched)(void *ctx, const msm_member *members, size_t n, const mfd_doc *doc, const msm_perfs *perf, const msw_weights *w);
    /* The texts: `fleet`, `groups`, `sched` (all, the per-program explains, the dry run) and the fleet
     * view's records (one a line). */
    void (*texts)(void *ctx, const char *fleet, const char *groups);
    void (*sched_texts)(void *ctx, const char *all, const msp_explains *explains, const char *dry, const char *records);
    void (*peer_state)(void *ctx, uint32_t state);
    void (*net_count)(void *ctx, uint32_t k, bool tx, uint8_t type, size_t bytes);
    void (*log)(void *ctx, bool error, const char *line);
    /* The state file could not be written: this member stops (fail-stop). */
    void (*fail_stop)(void *ctx, const char *why);
} mpr_host;

typedef struct mpr_relay mpr_relay;

/* Starts the fleet thread and its links for this topology (copied), with this member's capabilities and
 * its state file (state_path; NULL: in memory only, for tests). On error *err. */
mpr_relay *mpr_start(const mcc_topology *topo, const mpk_caps *caps, const char *state_path, const mpr_host *host, char **err);
/* Stops the thread and every link. */
void mpr_stop(mpr_relay *r);

/* A change asked for here (a control-port line): the leader queues it, another member forwards it,
 * without a leader it is refused. */
void mpr_change(mpr_relay *r, const char *line, mpr_reply reply);
/* The reconciler changed something the beats carry (status, the group's wish): beat now. */
void mpr_wake(mpr_relay *r);
/* The lockstep group as the reconciler wants it: ready for spec (NULL: quiesced; mirror: keep the
 * rounds going; member: in the group's epoch). */
void mpr_group_want(mpr_relay *r, const mpk_hello *spec, bool mirror, bool member);
/* The data plane's state (GL_*) once it saw the wish. */
uint32_t mpr_group_state(mpr_relay *r);
/* For the reconciler: an image from a member (the leader if linked), and a migrated view from its source;
 * block up to the given time. */
bool mpr_fetch_image(mpr_relay *r, const char *name, uint64_t hash, const char *dir, uint32_t wait_ms, mpr_payload *out, char **err);
bool mpr_fetch_view(mpr_relay *r, const char *name, uint64_t hash, uint32_t from, uint32_t wait_ms, uint32_t *view_abi, mpw_bytes *view, char **err);
/* An answer to a forwarded `job ...` request (F_CTL_REPLY to member to). */
void mpr_ctl_reply(mpr_relay *r, uint32_t to, uint32_t id, bool ok, const char *text);
/* Sends a frame to member k (bulk: blobs, behind every control frame). */
bool mpr_send(mpr_relay *r, uint32_t k, const uint8_t *f, size_t n, bool bulk);
void mpr_broadcast(mpr_relay *r, const uint8_t *f, size_t n);

/* The perf board: every member's F_PERF and the link RTTs (the scheduler's inputs). The daemon puts its
 * own report there and reads the echoes for its next F_PERF. */
void mpr_board_own(mpr_relay *r, const msr_report *rep);
/* The echoes for this member's next F_PERF: (member, its t_us we received last, µs held since). */
size_t mpr_board_echoes(mpr_relay *r, uint32_t *k, uint64_t *t, uint64_t *held);

/* For tests and the control port: the committed document's version, the leader, the role (MFC_*). */
uint64_t mpr_version(mpr_relay *r);
bool mpr_leader(mpr_relay *r, uint32_t *leader);
uint32_t mpr_role(mpr_relay *r);
uint32_t mpr_alive(mpr_relay *r);

#endif
