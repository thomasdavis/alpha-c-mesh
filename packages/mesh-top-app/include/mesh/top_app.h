/* @ajax/mesh-top-app: mesh-top's viewer state and key handling (crates/mesh-top src/app.rs). One
 * machine per endpoint (its latest snapshot and the history the dashboard derives rates from),
 * the tab, focus and selection, the prompts and the HTTP input. Keys become actions the caller
 * sends, so tests drive the app without sockets. Programs are bound by name ("doom", "train",
 * "infer", "http"). */
#ifndef MESH_TOP_APP_H
#define MESH_TOP_APP_H

#include "mesh/top_clock.h"
#include "mesh/top_net.h"
#include "mesh/top_snap.h"
#include "mesh/top_views.h"
#include "tui/input.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    MT_TAB_MESH,
    MT_TAB_PROGRAMS,
    MT_TAB_DOOM,
    MT_TAB_TRAIN,
    MT_TAB_INFER,
    MT_TAB_HTTP,
    MT_TAB_NET,
    MT_TAB_LOAD,
    MT_TAB_FLEET,
    MT_TAB_JOBS,
    MT_TAB_PIPES,
    MT_TAB_RENDER,
    MT_TABS
};
extern const char *const MT_TAB_NAMES[MT_TABS];
extern const char MT_TAB_KEYS[MT_TABS];
extern const char *const MT_VIEWS[3]; /* "fleet", "jobs", "pipes" */
extern const char *const MT_RENDER_PROGRAMS[2];

#define MT_VIEW_POLL MT_MS(1000)
#define MT_RENDER_POLL MT_MS(400)
#define MT_STALL MT_SECS(1)
#define MT_AUX_POLL MT_MS(250)
#define MT_LOG_LINES 200
#define MT_HTTP_INPUT_MAX 224
#define MT_SPARK_LEN 240
#define MT_PARAM_HISTORY 1024
#define MT_LOSS_POINTS 4096
#define MT_RATE_WINDOW MT_SECS(1)
#define MT_TRACE_STALE MT_MS(500)

/* A value sampled at snapshots, and its rate over MT_RATE_WINDOW (on the viewer's clock). */
typedef struct {
    mt_instant *t;
    uint64_t *v;
    size_t n, cap;
} mt_rate;
void mt_rate_push(mt_rate *r, mt_instant now, uint64_t v);
float mt_rate_per_s(const mt_rate *r);
void mt_rate_free(mt_rate *r);

typedef struct {
    uint32_t slot, node;
    uint32_t heartbeat;
    mt_instant changed;
    float util;
} mt_node_track;

typedef struct {
    uint32_t slot;
    char name[3 * MESH_NAME_BYTES + 1];
    uint32_t loads;
    mt_rate counters[MESH_COUNTERS];
} mt_prog_track;

typedef struct {
    uint32_t slot, n;
    mt_instant moved;
    mt_rec *recs;
    size_t nrecs;
} mt_trace_track;

typedef struct {
    uint64_t step;
    uint8_t view[32];
} mt_param;

typedef struct {
    uint64_t step;
    float loss;
} mt_loss;

/* A fleet-rendered image as a member assembles it (meshd `render <job> raw`). */
typedef struct {
    mt_kv *kv; /* the status's key value words */
    size_t nkv;
    struct {
        uint32_t k;
        uint64_t folded;
        uint32_t tiles;
    } *members;
    size_t nmembers;
    uint32_t (*tiles)[2]; /* (passes folded, member of the last) */
    size_t ntiles;
    uint8_t *px; /* tile-major 0xffBBGGRR */
    size_t npx;
    double msps;
} mt_fleet_image;
bool mt_fleet_image_parse(const uint8_t *b, size_t len, mt_fleet_image *out);
void mt_fleet_image_free(mt_fleet_image *f);
const char *mt_fleet_image_get(const mt_fleet_image *f, const char *k); /* "" when absent */
uint64_t mt_fleet_image_num(const mt_fleet_image *f, const char *k);

typedef struct {
    char *endpoint;
    bool online;
    char *status;
    bool has_snap;
    mt_snapshot snap;
    bool has_snap_at;
    mt_instant snap_at;
    mt_node_track *nodes; /* sorted by (slot, node) */
    size_t nnodes;
    mt_prog_track *progs; /* sorted by slot */
    size_t nprogs;
    uint64_t *tps; /* oldest first, at most MT_SPARK_LEN */
    size_t ntps;
    mt_param *params; /* sorted by step, at most MT_PARAM_HISTORY */
    size_t nparams;
    mt_loss *losses;
    size_t nlosses, caplosses;
    mt_rate train_steps, doom_shown;
    bool has_http_resp;
    uint8_t *http_resp;
    size_t http_resp_len;
    mt_trace_track *trace; /* sorted by slot */
    size_t ntrace;
    char *trace_err;
    char **log;
    size_t nlog;
    char *log_err;
    char **catalog;
    size_t ncatalog;
    mt_net_track net;
    uint64_t (*load_hist)[2]; /* (GPU share %, CPU busy %), at most MT_SPARK_LEN */
    size_t nload_hist;
    uint32_t load_seen;
    bool has_render;
    char *render_name;
    uint8_t *render_px;
    size_t render_len;
    char *render_err;
    bool has_fleet;
    mt_fleet_image fleet;
    bool has_fleet_prev;
    mt_instant fleet_prev_t;
    uint64_t fleet_prev_n;
} mt_machine;

/* The machine's name: its hostname once a snapshot told it, else the endpoint. out: 97 + endpoint. */
const char *mt_machine_name(const mt_machine *m, char *out, size_t cap);
bool mt_machine_system(const mt_machine *m, uint32_t *k);
const mt_section *mt_machine_program(const mt_machine *m, const char *name);
float mt_machine_counter_rate(const mt_machine *m, uint32_t slot, size_t i);
bool mt_machine_stalled(const mt_machine *m, uint32_t slot, uint32_t node, mt_instant now);
const mt_node_track *mt_machine_node(const mt_machine *m, uint32_t slot, uint32_t node);

typedef struct {
    mt_rec rec;
    uint32_t age;
} mt_flame_rec;
/* The flame chart's records with their end's age in ns within window_ns of the newest (malloc'd). */
size_t mt_machine_flame(const mt_machine *m, mt_instant now, uint32_t window_ns, mt_flame_rec **out);
/* Trace records of programs whose trace moved recently (malloc'd). */
size_t mt_machine_fresh_trace(const mt_machine *m, mt_instant now, mt_rec **out);

/* train's u64 next step, and its loss ring in step order (count written to out, at most 256). */
uint64_t mt_train_next_step(const mt_section *s);
size_t mt_train_losses(const mt_section *s, mt_loss out[256]);

/* The lockstep programs' view prefixes compared at the latest step every machine reported. */
typedef struct {
    uint64_t step;
    size_t n;
    bool identical;
    char differ[MESH_MAX_SYSTEMS * 4][64];
    size_t ndiffer;
} mt_identity;

/* A program's placement (from its manifest extension). */
typedef enum { MT_PLACE_LOCAL, MT_PLACE_PIN, MT_PLACE_SPLIT } mt_place_kind;
typedef struct {
    mt_place_kind kind;
    uint32_t pin;
    uint8_t assign[MESH_PLACE_PARTS_MAX];
} mt_place;
bool mt_place_of(const mt_section *s, mt_place *out); /* false: it declares no placement */
void mt_place_label(mt_place p, char out[32]);
uint32_t mt_place_input_to(mt_place p, uint32_t me);
mt_place mt_place_next(mt_place p, uint32_t n);
bool mt_place_eq(mt_place a, mt_place b);
typedef struct {
    uint32_t first, last, system;
} mt_part_run;
size_t mt_part_runs(const uint8_t assign[MESH_PLACE_PARTS_MAX], uint32_t fanout, mt_part_run *out);

typedef enum { MT_ACTION_NONE, MT_ACTION_QUIT, MT_ACTION_SEND, MT_ACTION_POLL } mt_action_kind;
typedef struct {
    mt_action_kind kind;
    size_t machine;
    char *line; /* malloc'd for SEND and POLL; free with mt_action_free */
} mt_action;
void mt_action_free(mt_action *a);

typedef enum { MT_PROMPT_NONE, MT_PROMPT_LOAD, MT_PROMPT_UNLOAD, MT_PROMPT_CONFIRM } mt_prompt_kind;
typedef struct {
    mt_prompt_kind kind;
    char input[132]; /* Load */
    bool has_pick;
    size_t pick;
    size_t machine;   /* Unload, Confirm */
    char *name;       /* Unload */
    char *line, *what; /* Confirm */
} mt_prompt;

typedef struct {
    mt_machine *machines;
    size_t nmachines;
    int tab;
    size_t focus, sel;
    mt_prompt prompt;
    char http_input[MT_HTTP_INPUT_MAX + 8];
    uint16_t http_scroll;
    char *last_reply;
    bool has_aux_at;
    mt_instant aux_at;
    bool has_view[3];
    mt_view views[3];
    bool has_view_at;
    mt_instant view_at;
    size_t fsel[2];
    bool has_jsel;
    size_t jsel;
    bool render_marks;
    bool has_render_at;
    mt_instant render_at;
} mt_app;

void mt_app_init(mt_app *app, const char *const *endpoints, size_t n);
void mt_app_free(mt_app *app);

typedef enum { MT_UPDATE_ONLINE, MT_UPDATE_OFFLINE, MT_UPDATE_SNAP, MT_UPDATE_SNAP_ERROR, MT_UPDATE_REPLY } mt_update_kind;
typedef struct {
    mt_update_kind kind;
    size_t machine;
    char *text;       /* Offline: why; SnapError: the error; Reply: the request line */
    mt_snapshot snap; /* Snap (moved into the app) */
    bool bytes;       /* Reply: framed bytes (else a text line) */
    uint8_t *data;    /* Reply: the bytes or the text */
    size_t len;
} mt_update;
/* Applies an update; takes ownership of its allocations. */
void mt_app_apply(mt_app *app, mt_update *u, mt_instant now);
/* Background refreshes due now; returns the count (at most 2), into out. */
size_t mt_app_tick(mt_app *app, mt_instant now, mt_action out[2]);
mt_action mt_app_handle_key(mt_app *app, tui_key k);

size_t mt_app_view_machine(const mt_app *app, size_t view);
int mt_app_tab_view(const mt_app *app); /* -1: none */
const mt_view *mt_app_view(const mt_app *app, size_t v);
/* The Fleet tab's selection: program name (NULL: none; caller frees) and member id. */
void mt_app_fleet_sel(const mt_app *app, char **prog, bool *has_member, uint32_t *member);
char *mt_app_job_sel(const mt_app *app); /* caller frees; NULL: none */
uint32_t mt_app_cluster_size(const mt_app *app);
bool mt_app_render_program(const mt_app *app, char name[64], uint32_t *tiles);
const mt_machine *mt_app_focused(const mt_app *app);
const mt_section *mt_app_selected(const mt_app *app);
bool mt_params_identity(const mt_app *app, mt_identity *out);

/* Constructors for tests and the client. */
mt_update mt_update_snap(size_t i, mt_snapshot s);
mt_update mt_update_reply_bytes(size_t i, const char *line, const void *data, size_t len);
mt_update mt_update_reply_text(size_t i, const char *line, const char *text);
mt_update mt_update_offline(size_t i, const char *why);
mt_update mt_update_online(size_t i);

#endif
