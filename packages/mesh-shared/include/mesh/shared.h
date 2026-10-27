/* @ajax/mesh-shared: state shared by meshd's threads, and the messages they
 * send the driver (commands), the fleet thread (events) and the jobs thread
 * (shared.rs).
 *
 * The driver thread is the only ring publisher and program-table writer:
 * every other thread asks it through `cmd` and waits on a one-shot reply. In
 * a fleet every lifecycle change comes from the reconciler (the committed
 * desired state); the control port goes through the leader (`change`).
 *
 * Payloads that belong to the fleet and jobs areas (placement and fleet
 * messages, links, beats, blob lists) travel as opaque pointers with their
 * free function: this package only carries them. */
#ifndef MESH_SHARED_H
#define MESH_SHARED_H

#include "mesh/abi.h"
#include "mesh/chan.h"
#include "mesh/host_block.h"
#include "mesh/infer_svc.h"
#include "mesh/jobs/frame.h"
#include "mesh/loader.h"
#include "mesh/mailbox.h"
#include "mesh/reply.h"
#include "mesh/views.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- where a load's or an upgrade's image comes from (Src) ---------------------------- */

typedef struct {
    bool is_bytes;        /* false: a catalog dir under --programs */
    char *dir;            /* the catalog dir */
    mesh_payload payload; /* bytes (the local catalog's, or a member's F_IMAGE) ... */
    uint64_t hash;        /* ... and the image hash they must have */
} mesh_src;

mesh_src mesh_src_catalog(const char *dir);
mesh_src mesh_src_bytes(const mesh_payload *p, uint64_t hash); /* copies p */
mesh_src mesh_src_copy(const mesh_src *s);
void mesh_src_free(mesh_src *s);
/* "dir" or "dir (from the peer)", as the driver's log lines name a source. */
char *mesh_src_what(const mesh_src *s);

/* ---- driver commands (Cmd) ------------------------------------------------------------ */

typedef enum {
    MESH_CMD_LOAD,       /* src, run (-1: the manifest's autostart), reply (may be NULL) */
    MESH_CMD_UPGRADE,    /* name, src (has_src false: the slot's current catalog dir), reply */
    MESH_CMD_ROLLBACK,   /* name, reply */
    MESH_CMD_UNLOAD,     /* name, now, reply */
    MESH_CMD_PROG_RUN,   /* name, on, reply */
    MESH_CMD_TASK,       /* name, op, arg, reply (Ok value) */
    MESH_CMD_REQ,        /* name, bytes, reply (Ok bytes) */
    MESH_CMD_INPUT,      /* name, bytes, reply */
    MESH_CMD_RESET,      /* name, reply */
    MESH_CMD_PLACE,      /* name, words (has_words false: report), reply */
    MESH_CMD_PEER_PLACE, /* msg (a placement message), from */
    MESH_CMD_PEER_FRAME, /* msg (a frame message) */
    MESH_CMD_PEER_INPUT, /* name, bytes */
    MESH_CMD_FLEET_VIEW, /* up, has_leader/leader, places */
    MESH_CMD_INVENTORY,  /* reply (Ok ptr: mesh_actuals) */
    MESH_CMD_VIEW_GET,   /* name, reply (Ok ptr: mesh_view_bytes) */
    MESH_CMD_VIEW_PUT,   /* name, view_abi, bytes, reply */
    MESH_CMD_JOB_LEASE,  /* lease */
    MESH_CMD_JOB_CUT,    /* lease_id, cut */
    MESH_CMD_JOB_PROGS,  /* reply (Ok ptr: mesh_job_progs) */
} mesh_cmd_kind;

/* A committed placement request (FleetView). */
typedef struct {
    char *name;
    char **words;
    size_t nwords;
} mesh_place_req;

typedef struct {
    mesh_cmd_kind k;
    char *name;
    bool has_src;
    mesh_src src;
    int run;
    bool now, on;
    uint32_t op, arg;
    uint8_t *bytes;
    size_t len;
    bool has_words;
    char **words;
    size_t nwords;
    void *msg;
    void (*msg_free)(void *msg);
    uint32_t from;
    uint32_t up;
    bool has_leader;
    uint32_t leader;
    mesh_place_req *places;
    size_t nplaces;
    uint32_t view_abi;
    mj_lease_msg lease;
    uint64_t lease_id, cut;
    mesh_reply *reply;
} mesh_cmd;

mesh_cmd mesh_cmd_new(mesh_cmd_kind k);
/* Frees what the command owns; an unanswered reply is dropped (its waiter sees that at once). */
void mesh_cmd_free(mesh_cmd *c);

/* Every loaded program as it is now (Inventory: the reconciler's input; fleet.rs Actual). */
typedef struct {
    char *name, *dir;
    uint64_t hash;
    bool has_prev;
    uint64_t prev;
    bool run, failed, lockstep;
} mesh_actual;

typedef struct {
    mesh_actual *v;
    size_t n;
} mesh_actuals;

void mesh_actuals_free(void *p); /* a heap mesh_actuals */

/* A program's view in flight between members (a migration). */
typedef struct {
    uint32_t view_abi;
    uint8_t *bytes;
    size_t len;
} mesh_view_bytes;

void mesh_view_bytes_free(void *p); /* a heap mesh_view_bytes */

/* The job programs running here (jobs.rs Prog). */
typedef struct {
    mj_prog *v;
    size_t n;
} mesh_job_progs;

void mesh_job_progs_free(void *p);

/* ---- fleet thread events (PeerEvent) -------------------------------------------------- */

typedef enum {
    MESH_PEV_LINK,       /* msg: a JOINed link */
    MESH_PEV_CLOSED,     /* from, lid, text (why) */
    MESH_PEV_FLEET,      /* from, lid, msg */
    MESH_PEV_BEAT,       /* from, lid, msg */
    MESH_PEV_HELLO,      /* from, lid, msg */
    MESH_PEV_CTL,        /* from, lid, id, line */
    MESH_PEV_CTL_REPLY,  /* from, lid, id, ok, text */
    MESH_PEV_IMAGE_REQ,  /* from, lid, id, name, hash, dir */
    MESH_PEV_IMAGE,      /* from, lid, id, msg (or ok false and text) */
    MESH_PEV_CHANGE,     /* line, reply (NULL: nobody waits) */
    MESH_PEV_FETCH_IMAGE,/* name, hash, dir, reply */
    MESH_PEV_WAKE,
    MESH_PEV_VIEW_REQ,   /* from, lid, id, name, hash */
    MESH_PEV_VIEW,       /* from, lid, id, msg */
    MESH_PEV_FETCH_VIEW, /* name, hash, from, reply */
} mesh_peer_event_kind;

typedef struct {
    mesh_peer_event_kind k;
    uint32_t from, id;
    uint64_t lid, hash;
    bool ok;
    char *line, *text, *name, *dir;
    void *msg;
    void (*msg_free)(void *msg);
    mesh_reply *reply;
} mesh_peer_event;

mesh_peer_event mesh_peer_event_new(mesh_peer_event_kind k);
void mesh_peer_event_free(mesh_peer_event *e);

/* ---- jobs thread events (JobEvent) ---------------------------------------------------- */

typedef enum {
    MESH_JEV_FRAME,        /* from, ty, body */
    MESH_JEV_CTL,          /* line, reply_to (LOCAL: reply; REMOTE: remote_from, remote_id) */
    MESH_JEV_DONE,         /* res, has_payload/payload */
    MESH_JEV_BLOBS_STORED, /* msg */
    MESH_JEV_BLOB_ACKED,   /* from, msg, ok */
    MESH_JEV_BLOB_GONE,    /* msg */
} mesh_job_event_kind;

typedef enum { MESH_JOB_REPLY_NONE, MESH_JOB_REPLY_LOCAL, MESH_JOB_REPLY_REMOTE } mesh_job_reply;

typedef struct {
    mesh_job_event_kind k;
    uint32_t from;
    uint8_t ty;
    uint8_t *body;
    size_t len;
    char *line;
    mesh_job_reply reply_to;
    mesh_reply *reply;
    uint32_t remote_from, remote_id;
    mj_item_res res;
    bool has_payload;
    uint8_t *payload;
    size_t payload_len;
    bool ok;
    void *msg;
    void (*msg_free)(void *msg);
} mesh_job_event;

mesh_job_event mesh_job_event_new(mesh_job_event_kind k);
void mesh_job_event_free(mesh_job_event *e);

/* ---- link frames ------------------------------------------------------------------------ */

/* A frame for a peer link (type byte first); an empty one is a doorbell. */
typedef struct {
    uint8_t *b;
    size_t n;
} mesh_link_frame;

/* A bounded queue of link frames (a link's writer drains it). */
mesh_chan *mesh_link_queue_new(size_t bound);

/* ---- the shared state ------------------------------------------------------------------ */

#define MESH_PEER_SINGLE 0u
#define MESH_PEER_WAITING 1u
#define MESH_PEER_UP 2u

typedef struct {
    double sum;
    uint32_t n;
} mesh_rtt_acc;

/* A sched explain text per program. */
typedef struct {
    char *name, *text;
} mesh_explain;

typedef struct mesh_shared {
    mesh_mailbox mb;
    mesh_host_block host;
    char *programs; /* --programs DIR: the only place `load` reads from */
    mesh_chan *cmd;  /* mesh_cmd, to the driver */
    mesh_chan *peer; /* mesh_peer_event, to the fleet thread; NULL single */
    uint32_t me, n;  /* this system and the cluster's size (0 and 1 single) */
    _Atomic uint32_t peer_state;
    _Atomic bool lock_feed; /* the lockstep program may be fed */
    /* Per member, over the current second: exchange round trips and waits for its rounds (under mu). */
    mesh_rtt_acc rtt[MESH_MAX_SYSTEMS], wait[MESH_MAX_SYSTEMS];
    _Atomic uint64_t round_of[MESH_MAX_SYSTEMS];
    _Atomic bool ready; /* the startup loads are done */
    _Atomic bool *quit; /* set by SIGINT/SIGTERM */
    _Atomic bool fatal; /* set by fail_stop */
    /* Each linked member's frame queue and bulk queue (mesh_link_frame), under mu; NULL: no link. */
    mesh_chan *link_out[MESH_MAX_SYSTEMS], *bulk_out[MESH_MAX_SYSTEMS];
    mesh_chan *blobs; /* the blob thread's events (jobs area); its messages are that area's */
    _Atomic uint64_t blob_mem;
    mesh_chan *jobs; /* mesh_job_event, to the jobs thread */
    /* Texts and views other threads refresh (under mu): `fleet` and `groups`, the scheduler's, the views. */
    char *fleet_text, *groups_text;
    char *sched_all, *sched_dry;
    mesh_explain *sched_explain;
    size_t nexplain;
    mesh_recs fleet_view, jobs_view_recs;
    /* The pipes view's last sample per inbox (control.rs pipes_recs), under mu. */
    struct mesh_pipe_sample *pipe_hist;
    size_t npipe_hist;
    /* Where the members' inference services listen (set once at start). */
    bool has_svc;
    mesh_infer_svc svc;
    /* The fleet's and the jobs' own state (their areas own these). */
    void *fleet_ext, *jobs_ext;
    pthread_mutex_t mu;
} mesh_shared;

/* The pipes view's sample of an inbox: when, head, tail, full, rates in/out/refused per s. */
typedef struct mesh_pipe_sample {
    char name[MESH_NAME_BYTES + 1];
    int64_t at_ns;
    uint32_t head, tail, full;
    float rates[3];
} mesh_pipe_sample;

/* A Shared for meshd, tests and tools: mailbox mb, member me of n (n = 1: single; peered: a fleet thread's channel). */
mesh_shared *mesh_shared_new(mesh_mailbox mb, const char *programs, bool peered, uint32_t me, uint32_t n, _Atomic bool *quit);
void mesh_shared_free(mesh_shared *sh);

/* Microseconds since this process started (a monotonic clock for link RTTs). */
uint64_t mesh_shared_mono_us(void);

/* Queues a frame for system to (copied); false when it is not linked, its queue is full, or after fail_stop. */
bool mesh_shared_send_peer(mesh_shared *sh, uint32_t to, const uint8_t *frame, size_t n);
/* Waits up to within_ms for room in to's queue. */
bool mesh_shared_send_peer_wait(mesh_shared *sh, uint32_t to, const uint8_t *frame, size_t n, int64_t within_ms);
/* A bulk frame (a blob), written when the control frames are out; a doorbell wakes the writer. */
bool mesh_shared_send_bulk(mesh_shared *sh, uint32_t to, const uint8_t *frame, size_t n);
/* Every linked member in mask; false if none is or a queue refused it. */
bool mesh_shared_send_mask(mesh_shared *sh, uint32_t mask, const uint8_t *frame, size_t n);
bool mesh_shared_broadcast(mesh_shared *sh, const uint8_t *frame, size_t n);
/* Fail-stop: this member's state could not be made durable; nothing more leaves it, and meshd shuts down. */
void mesh_shared_fail_stop(mesh_shared *sh, const char *why);

/* A driver command with a one-line reply, bounded in time. cmd's reply is set
 * here (any given one is replaced). Returns the Ok text or the Err text in
 * *text (free it) and whether it was Ok. "driver gone", "no answer from the
 * driver in 60s" as the Rust. */
bool mesh_shared_ask(mesh_shared *sh, mesh_cmd cmd, int64_t within_ms, char **text);
void mesh_shared_set_peer_state(mesh_shared *sh, uint32_t s);
/* A change to the desired state through the fleet (the leader commits it). */
bool mesh_shared_change(mesh_shared *sh, const char *line, int64_t within_ms, char **text);
/* An HTTP request head through the GPU's http program (`req http`); counted in
 * the host block. Ok: *resp (free it). Err: *err. */
bool mesh_shared_http(mesh_shared *sh, const uint8_t *head, size_t n, uint8_t **resp, size_t *rn, char **err);

/* Locked accessors for the texts other threads refresh: a copy (free it). */
char *mesh_shared_text(mesh_shared *sh, char *const *field);
void mesh_shared_set_text(mesh_shared *sh, char **field, const char *text);

#endif
