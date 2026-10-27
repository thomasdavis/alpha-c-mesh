/* The fleet thread's state, shared by its source files (relay.c: events, links, changes, the leader;
 * group.c: the lockstep data plane; sched.c: the scheduler glue and migrations; publish.c: the status
 * block and the texts; board.c: the perf board). Not installed. */
#ifndef RELAY_H
#define RELAY_H

#include "mesh/fleet_core.h"
#include "mesh/fleet_store.h"
#include "mesh/peer_link.h"
#include "mesh/peer_relay.h"
#include "mesh/peer_view.h"
#include "mesh/sched_place.h"
#include "mesh/sched_tracker.h"
#include "mesh/mt.h"

#include <pthread.h>
#include <stdatomic.h>

#define MAXS MESH_MAX_SYSTEMS

uint64_t mpr_mono_ms(void);
uint64_t mpr_mono_us(void);

/* One link and what the data plane keeps per link. */
typedef struct {
    struct mpr_relay *r;
    mpn_link *l;
    uint32_t from;
    uint64_t lid;
    atomic_uint_fast64_t last_rx; /* this meshd's uptime ms when its last frame arrived */
    pthread_mutex_t sent_mu;      /* our rounds in flight to it: (counter, sent µs) */
    uint64_t sent_c[64], sent_t[64];
    size_t sent_n;
    atomic_uint_fast64_t last_round; /* its last round counter seen here */
    atomic_int refs;                 /* the rounds sender writing to it (freeing waits for 0) */
} linkrec;

typedef enum { EV_LINK, EV_CLOSED, EV_FRAME, EV_CHANGE, EV_WAKE, EV_FETCH_IMAGE, EV_FETCH_VIEW, EV_CTL_REPLY } ev_kind;

/* A blocked caller's answer slot (fetches). */
typedef struct {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    bool done, ok;
    char *err;
    mpr_payload payload;
    uint32_t view_abi;
    mpw_bytes view;
} waiter;

typedef struct {
    ev_kind kind;
    uint32_t from, id;
    uint64_t lid, hash;
    int fd;
    mpk_join join;
    atomic_bool *alive;
    char *text, *name, *dir;
    mpw_bytes frame;
    mpr_reply reply;
    waiter *w;
    bool ok;
} event;

/* Who waits for a change's answer: a local caller (or nobody), or a member's F_CTL id. */
typedef struct {
    bool remote;
    uint32_t k, id;
    mpr_reply local;
} replyto;

/* The change the leader is committing or waiting on. */
typedef struct {
    char *what;
    replyto reply;
    uint64_t version;
    bool committed;
    uint32_t up;       /* the members up when it committed ... */
    uint64_t deadline; /* ... and when we stop waiting for them (ms) */
    mfd_doc before, after;
} pending_t;

/* The leader's migration in flight. */
typedef struct {
    mst_move mv;
    uint64_t version, t0_ms;
} mig_t;

typedef struct {
    bool used;
    uint32_t id;
    waiter *w;
    uint32_t from;
    uint64_t deadline;
} image_wait;

typedef struct {
    bool used;
    uint32_t id;
    waiter *w;
    mpv_wait v;
} view_wait;

typedef struct {
    bool used;
    uint32_t id;
    mpr_reply reply;
    uint32_t leader;
    uint64_t deadline;
} fwd_t;

/* A failure the scheduler avoids a while: (program, member) -> (hash, why, unsupported, when). */
typedef struct {
    char *name;
    uint32_t k;
    uint64_t hash;
    char *why;
    bool unsupported;
    uint64_t at;
} fail_mem_t;

typedef struct {
    char *name;
    uint32_t k;
    msm_perf perf;
    uint64_t at;
} perf_mem_t;

/* The perf board: every member's last F_PERF (and when it came), and the link RTTs. */
typedef struct {
    pthread_mutex_t mu;
    bool has[MAXS];
    msr_report rep[MAXS];
    uint64_t at_us[MAXS];
    mof32 rtt_us[MAXS];
} board_t;

/* The lockstep rounds' tag: (epoch, cycle) and each system's rank (0xff: not in it). */
typedef struct {
    uint32_t epoch, cycle;
    uint8_t rank_of[MAXS], me;
} tag_t;

struct mpr_relay {
    mpr_host h;
    uint32_t me, n;
    mcc_tunables tun;
    mpk_caps my_caps;
    mfc_core *core;
    mfs_store store;
    bool has_store;
    uint64_t t0_ms;
    /* events */
    pthread_mutex_t ev_mu;
    pthread_cond_t ev_cv;
    event *ev;
    size_t ev_n, ev_cap;
    pthread_t thread, sender;
    atomic_bool quit, fatal;
    /* links */
    pthread_mutex_t links_mu;
    linkrec *links[MAXS];
    mpk_caps caps[MAXS];
    bool has_caps[MAXS];
    uint64_t next_lid;
    mpn_acceptor *acceptor;
    mpn_dialer *dialers[MAXS];
    size_t ndialers;
    /* members' beats */
    mpk_beat_status st[MAXS];
    uint64_t last_beat_ms;
    /* changes */
    char **queue_line;
    replyto *queue_to;
    size_t nqueue;
    bool has_pending;
    pending_t pending;
    pending_t *waiting;
    size_t nwaiting;
    fwd_t fwd[64];
    image_wait images[32];
    view_wait views[32];
    uint32_t next_id;
    ms_map paused_since; /* group -> ms */
    bool has_first_doc_at;
    uint64_t first_doc_at_ms;
    uint32_t seen_status[MAXS];
    char *seen_reason[MAXS];
    /* the driver's last view */
    bool has_view;
    uint32_t view_up;
    bool view_has_leader;
    uint32_t view_leader;
    char *view_places;
    /* the lockstep group */
    pthread_mutex_t group_mu; /* the reconciler's wish */
    bool want_set, want_mirror, want_member, want_changed;
    mpk_hello want;
    bool has_hello;
    mpk_hello hello;
    bool has_theirs[MAXS];
    mpk_hello theirs[MAXS];
    atomic_uint hello_state;
    char *hello_why;
    uint64_t hello_sent_ms;
    bool has_mismatch[MAXS];
    mpk_hello mismatch[MAXS];
    atomic_bool mirroring;
    pthread_mutex_t send_mu; /* held by the sender for a whole pass (a retag waits for it) */
    pthread_mutex_t tag_mu;
    tag_t tag;
    atomic_uint_fast64_t last_sent;
    pthread_mutex_t own_mu;
    uint64_t own_c[64], own_t[64];
    size_t own_n;
    /* publishing */
    uint64_t last_publish_ms, last_sched_text_ms;
    /* the scheduler */
    msw_weights w;
    mst_tracker *tracker;
    bool has_mig;
    mig_t mig;
    bool down[MAXS];
    uint64_t down_since[MAXS];
    perf_mem_t *perf_mem;
    size_t nperf_mem;
    uint64_t last_sched, last_rebalance;
    uint32_t moves_done, last_gap_us;
    mt_strs sched_log;
    char *group_prop;
    msp_explains explains;
    fail_mem_t *fail_mem;
    size_t nfail_mem;
    bool has_load[MAXS];
    mpv_load load_avg[MAXS];
    board_t board;
    /* What other threads may read without the core: set at the end of every tick. */
    atomic_uint_fast64_t pub_version;
    atomic_uint pub_leader, pub_role, pub_alive;
};

/* relay.c */
uint64_t relay_now(const mpr_relay *r);
void relay_send(mpr_relay *r, uint32_t k, const mpw_bytes *f);
void relay_broadcast(mpr_relay *r, const mpw_bytes *f);
void relay_log(mpr_relay *r, bool error, char *line); /* takes line */
bool relay_propose(mpr_relay *r, mfd_doc doc, char *what, replyto to, uint64_t *version);
uint32_t relay_linked_mask(mpr_relay *r);
bool relay_linked(mpr_relay *r, uint32_t k);
uint64_t relay_last_rx(mpr_relay *r, uint32_t k, bool *linked);
void relay_beat_all(mpr_relay *r);
void relay_my_status(mpr_relay *r, mpk_beat_status *out);
void relay_reply(mpr_relay *r, replyto to, bool ok, const char *text);
void waiter_done(waiter *w, bool ok, char *err);

/* group.c */
void group_want(mpr_relay *r);
void group_hello(mpr_relay *r, uint32_t from, const mpk_hello *spec);
void group_hello_send(mpr_relay *r);
void group_pause(mpr_relay *r, const char *why);
void group_set_state(mpr_relay *r, uint32_t s, const char *why);
void group_round(mpr_relay *r, linkrec *lr, const uint8_t *body);
void *group_sender(void *arg);

/* sched.c (sched_publish: publish.c) */
msm_member *sched_members(mpr_relay *r, uint64_t now, size_t *n);
void sched_members_free(msm_member *ms, size_t n);
msm_perfs sched_perf_map(mpr_relay *r, uint64_t now);
mfd_doc sched_pass(mpr_relay *r, const mfd_doc *doc, uint64_t now, mt_strs *changes);
mfd_doc sched_edit(mpr_relay *r, const mfd_doc *base, mfd_doc doc, uint64_t now, mt_strs *changes);
void sched_schedule(mpr_relay *r, uint64_t now);
void sched_serve_view(mpr_relay *r, uint32_t k, uint32_t id, const char *name, uint64_t hash);
void sched_fetch_view(mpr_relay *r, const char *name, uint64_t hash, uint32_t from, waiter *w);
void sched_view_chunk(mpr_relay *r, uint32_t id, const uint8_t *body, size_t n);
void sched_view_timeouts(mpr_relay *r, bool has_lost, uint32_t lost);
void sched_publish(mpr_relay *r, uint64_t now, uint32_t alive, uint32_t suspect, const mpr_recon *rs);
void sched_log_decision(mpr_relay *r, const char *what);

/* publish.c */
void publish_all(mpr_relay *r, uint64_t now);
void publish_member(mpr_relay *r, uint32_t k, uint32_t state, const mpk_caps *caps);
char *publish_group_line(const mfd_group *g);

/* board.c */
void board_init(board_t *b);
void board_free(board_t *b);
/* An F_PERF from member from (its text). */
void board_report(mpr_relay *r, uint32_t from, const char *text);

#endif
