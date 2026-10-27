/* The driver's state (driver.rs Driver, programs.rs Image/Slot/Phase), shared by its source files. */
#ifndef MESH_DRIVER_INT_H
#define MESH_DRIVER_INT_H

#include "mesh/driver.h"

#include "mesh/abi.h"
#include "mesh/buf.h"
#include "mesh/jobs/frame.h"
#include "mesh/jobs/ranges.h"
#include "mesh/log.h"
#include "mesh/manifest_encode.h"
#include "mesh/placer.h"
#include "mesh/prog_table.h"
#include "mesh/publish.h"
#include "mesh/view_alloc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ticks and bounds, ns. */
#define TICK_NS 500000ll
#define FAST_TICK_NS 50000ll
#define DRAIN_NS 3000000000ll
#define HALT_NS 3000000000ll
#define ONLINE_NS 2000000000ll
#define MAX_QUEUED 256
#define PIPE_SAMPLE_NS 2000000ll
#define PIPE_DEADLINE_NS 500000000ll

/* A program image loaded into the context (programs.rs Image). */
typedef struct {
    mesh_manifest manifest;
    char *dir;
    mesh_kernel *kernel;
    uint64_t hash;
    uint32_t code_bytes, regs, per_sm, reg_cap, local, rt_abi, exec;
} dimage;

typedef enum { PH_STOPPED, PH_STARTING, PH_RUNNING, PH_DRAINING, PH_HALTING } dphase;

typedef struct {
    uint32_t op, arg;
    mesh_reply *reply;
} dtask;

typedef struct {
    uint8_t *bytes;
    size_t len;
    mesh_reply *reply;
} dreq;

typedef struct {
    dimage image;
    bool has_prev;
    dimage prev;
    mesh_stream *stream;
    uint32_t view_off, loads, reads_mask, feeds_mask, fed_mask;
    dphase phase;
    int64_t phase_t0;
    bool want_run;
    uint32_t run_seq;
    bool needs_init;
    char *failed; /* NULL: not failed */
    bool unloading;
    mesh_pring ring;
    int64_t last_auto;
    uint32_t last_done;
    float rate;
    dtask *tasks;
    size_t ntasks, ctasks;
    dreq *reqs;
    size_t nreqs, creqs;
} dslot;

/* Lifecycle work that needs the JIT worker first. */
typedef struct {
    bool upgrade;
    mesh_src src;
    int run;        /* load: -1 none, 0, 1 */
    uint32_t slot;  /* upgrade */
    mesh_reply *reply;
} djob;

typedef enum { OP_LOAD, OP_UPGRADE, OP_ROLLBACK, OP_RESET, OP_FREE, OP_REBIND } dop_kind;

typedef struct {
    dop_kind k;
    mesh_prepared *p; /* LOAD, UPGRADE */
    int run;          /* LOAD */
    uint32_t slot;    /* UPGRADE, ROLLBACK, RESET */
    mesh_reply *reply;
    bool has_scope;   /* false: the whole GPU context */
    uint32_t scope;
    uint32_t exec;
    int64_t since;
} dblink;

typedef struct {
    uint32_t slot;
    bool on, unload, now;
    mesh_reply *reply;
    int64_t t0;
} dwaiter;

typedef struct {
    mesh_kernel *kernel; /* NULL: only a view */
    uint32_t view_off, view_bytes;
    uint32_t exec;
    bool has_inbox; /* the view held an inbox */
    uint32_t fed;
    int64_t since;
    bool warned;
} dzombie;

typedef struct {
    uint32_t consumer;
    mesh_pipe_inbox inbox;
    uint32_t side, producer, pos;
} ddead;

typedef struct {
    uint64_t lease, job;
    uint32_t op;
    mj_setup *setup;
    size_t nsetup;
    mj_ranges items;
} dlease;

typedef struct {
    dlease *q;
    size_t n, cap;
    bool has_ctx;
    uint64_t ctx_job, ctx_window;
    mesh_setup_state setup;
    uint32_t inflight;
} djobslot;

/* A placeable program's placement (place.rs), peered only (driver.rs PSlot). */
typedef struct {
    mpl_placer *p;
    char *name;
    bool has_follow; /* the follow batch to publish (split, a follower): its frame */
    uint32_t follow;
    bool stopped;    /* the placement stopped it here (pin elsewhere): it runs again when that ends */
    uint32_t part_ns;
} dplace;

/* A `place` request waiting for its change (by handle). */
typedef struct {
    uint64_t h;
    mesh_reply *reply;
} dplace_reply;

struct mesh_driver {
    mesh_shared *sh;
    mesh_backend *gpu;
    mesh_backend **cpus;
    size_t ncpus;
    mesh_driver_config cfg;
    int64_t last_stats, last_log;
    char *fault; /* a kernel faulted: the CUDA context is lost */
    dslot *slots[MESH_PROG_SLOTS];
    mesh_view_heap heap;
    dzombie *zombies;
    size_t nzombies, czombies;
    djob *jobs; /* FIFO */
    size_t njobs, cjobs;
    bool working;
    mesh_reply *work_rx;
    djob work_job;
    dblink *blinks; /* FIFO */
    size_t nblinks, cblinks;
    dwaiter *waiters;
    size_t nwaiters, cwaiters;
    uint32_t epoch;
    uint32_t lock_hi, lock_lo;
    bool state_dirty, restoring;
    /* A GET /load/<name> from the http program: its slot, HV_LOAD_SEQ, the load's answer. */
    bool gpu_load;
    uint32_t gpu_load_slot, gpu_load_seq;
    mesh_reply *gpu_load_rx;
    bool halting;
    int64_t halting_t;
    char *startup_error;
    struct { uintptr_t key; uint64_t cpu; } native_cpu[MESH_PROG_SLOTS];
    uint32_t me, n;
    uint32_t fleet_up;
    bool has_fleet_leader;
    uint32_t fleet_leader;
    djobslot jobw[MESH_PROG_SLOTS];
    /* Per slot: its placement (programs whose manifest declares `place`, when peered). */
    dplace *place[MESH_PROG_SLOTS];
    dplace_reply *place_replies;
    size_t nplace_replies, cplace_replies;
    uint64_t place_handle;
    /* The fleet's placement requests (FleetView). */
    mesh_place_req *fleet_places;
    size_t nfleet_places;
    ddead *dead;
    size_t ndead, cdead;
};

/* driver.c */
uint32_t drv_gpu_kind(const mesh_driver *d);
mesh_backend *drv_be(const mesh_driver *d, uint32_t exec);
uint32_t drv_exec_of(const mesh_driver *d, uint32_t s);
bool drv_in_scope(const mesh_driver *d, bool has_scope, uint32_t scope, uint32_t s);
int drv_find(const mesh_driver *d, const char *name);
const char *drv_name(const dslot *x);
bool drv_live(const dslot *x);
uint32_t drv_state(const dslot *x);
void drv_entry(const dslot *x, uint8_t out[MESH_PT_BYTES]);
void drv_pipe(const dslot *x, uint8_t out[MESH_PP_BYTES]);
void drv_ext(const dslot *x, uint8_t out[MESH_PX_BYTES]);
bool drv_view_compatible(const mesh_manifest *a, const mesh_manifest *b);
void drv_answer(mesh_reply *reply, bool ok, const char *text);
void drv_startup_failed(mesh_driver *d, const mesh_reply *reply, const char *e);
void drv_image_free(dimage *im);
void drv_push_blink(mesh_driver *d, dblink b);
void drv_push_job(mesh_driver *d, djob j);
void drv_program_cmd(mesh_driver *d, mesh_cmd *c);

/* lifecycle.c */
void drv_programs_tick(mesh_driver *d, int64_t now);
void drv_phase_step(mesh_driver *d, uint32_t s, int64_t now, bool in_blink);
bool drv_halted(mesh_driver *d, uint32_t s, bool *halted, char **err);
bool drv_launch_slot(mesh_driver *d, uint32_t s, char **err);
void drv_running(mesh_driver *d, uint32_t s, int64_t t0);
void drv_stopped(mesh_driver *d, uint32_t s);
void drv_fail(mesh_driver *d, uint32_t s, char *why);
void drv_resolve(mesh_driver *d, uint32_t s, bool on, bool ok, const char *text);
void drv_remove(mesh_driver *d, uint32_t s);
void drv_collect(mesh_driver *d, uint32_t s);
void drv_context_fault(mesh_driver *d, const char *e);
void drv_not_run(mesh_driver *d, uint32_t s, mesh_out *outs, size_t n);
int drv_input(mesh_driver *d, uint32_t s, const uint8_t *bytes, size_t n, char **text);
void drv_producer_stopped(mesh_driver *d, uint32_t s);
uint32_t drv_pipe_quiet(mesh_driver *d, uint32_t fed, int64_t limit_ns);
void drv_zero_exchange(mesh_driver *d);
void drv_connect_pipes(mesh_driver *d);
bool drv_pipe_check(mesh_driver *d, const mesh_manifest *m, bool has_except, uint32_t except, char **err);

/* blink.c */
void drv_jobs_tick(mesh_driver *d);
void drv_blink_tick(mesh_driver *d);
bool drv_op_reset(mesh_driver *d, uint32_t s, char **text);
bool drv_precheck(mesh_driver *d, const mesh_manifest *m, char **err);

/* place.c */
const char *drv_role_word(mpm_role r);
char *drv_placed_here(const mesh_driver *d, uint32_t s);
void drv_place_words(mesh_driver *d, uint32_t s);
void drv_place_acts(mesh_driver *d, bool has_s, uint32_t s, mpl_acts acts);
void drv_sync_place(mesh_driver *d, uint32_t s);
void drv_place_drop(mesh_driver *d, uint32_t s, const char *name);
void drv_place_free_all(mesh_driver *d);
bool drv_place_busy(const mesh_driver *d);
void drv_place_part_ns(mesh_driver *d);
void drv_place_log(mesh_driver *d);
void drv_place_tick(mesh_driver *d);
void drv_place_frame(mesh_driver *d, uint32_t s, bool own, bool follow);
void drv_place_follow_lost(mesh_driver *d, uint32_t s);
bool drv_place_stopped(const mesh_driver *d, const char *name);
void drv_place_publish(mesh_driver *d, uint32_t s, bool *paused, mpm_role *role, uint32_t *parts);
void drv_cmd_place(mesh_driver *d, mesh_cmd *c, mesh_reply *reply);
void drv_cmd_peer_place(mesh_driver *d, mesh_cmd *c);
void drv_cmd_peer_frame(mesh_driver *d, mesh_cmd *c);
void drv_cmd_peer_input(mesh_driver *d, mesh_cmd *c);
void drv_cmd_fleet_view(mesh_driver *d, mesh_cmd *c);
uint32_t drv_input_to(const mesh_driver *d, uint32_t s);

/* jobs.c */
void drv_publish_jobs(mesh_driver *d, uint32_t s);
void drv_job_done(mesh_driver *d, uint64_t lease, uint64_t job, uint64_t item, uint32_t status, uint32_t result, const char *out);
void drv_jobs_returned(mesh_driver *d, const dlease *l, uint32_t status, const char *why);
void drv_lease_free(dlease *l);
uint32_t drv_job_cap(const mesh_manifest *m, uint32_t exec);

#endif
