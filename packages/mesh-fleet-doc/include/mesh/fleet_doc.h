/* @ajax/mesh-fleet-doc: the fleet's desired state (fleet.rs Doc): the programs that should be loaded
 * (catalog dir, image hash, run state, a reset generation, the previous image for a rollback, the
 * scheduling needs, the scheduler's placement, a migration in flight, the operator's pin), the
 * lockstep groups (program, ordered members, epoch, cycle, state, on-loss policy), the placement
 * requests and the scheduler's switches. Text, one line per item; a few hundred bytes. The leader
 * sends it whole. */
#ifndef MESH_FLEET_DOC_H
#define MESH_FLEET_DOC_H

#include "mesh/abi/base.h"
#include "mesh/member_list.h"
#include "mesh/sched_needs.h"
#include "mesh/mt.h"

#include <stdbool.h>
#include <stdint.h>

/* A program the fleet should run (every member that can, or where the scheduler placed it). */
typedef struct {
    char *dir;          /* the catalog dir its image comes from */
    uint64_t hash;      /* the image hash */
    bool run;
    uint32_t reset;     /* bumped by `reset <name>` */
    bool lockstep;      /* peer = "lockstep": it runs as a lockstep group */
    mt_strs backends;   /* the manifest's backends */
    bool has_prev;      /* the image before the last upgrade, for `rollback` */
    char *prev_dir;
    uint64_t prev_hash;
    msn_needs needs;    /* the manifest's scheduling keys */
    ml_opt at;          /* the scheduler's placement (none: every member that can) */
    bool has_from;      /* a migration in flight: the member whose view the new member takes */
    uint32_t from;
    bool has_to;        /* the migration's target, and the version that started it */
    uint32_t to;
    uint64_t since;
    ml_opt pin;         /* `sched pin`: the operator's placement */
    char *why;          /* why it is where it is */
} mfd_prog;

/* A ProgSpec with Rust's empty/None defaults and these needs (copied). */
mfd_prog mfd_prog_new(const char *dir, uint64_t hash, bool run, const msn_needs *needs);
mfd_prog mfd_prog_clone(const mfd_prog *p);
bool mfd_prog_eq(const mfd_prog *a, const mfd_prog *b);
void mfd_prog_free(mfd_prog *p);
/* Whether member k should have it loaded (placed there, or the source of its migration). */
bool mfd_prog_here(const mfd_prog *p, uint32_t k);

/* What a lockstep group's latest cycle did. */
typedef enum { MFD_OP_NONE, MFD_OP_UPGRADE, MFD_OP_ROLLBACK, MFD_OP_TASK } mfd_op_kind;
typedef struct {
    mfd_op_kind kind;
    uint32_t op, arg; /* a task's */
} mfd_op;

typedef enum { MFD_RUNNING, MFD_PAUSED } mfd_gstate;
typedef enum { MFD_LOSS_PAUSE, MFD_LOSS_REFORM } mfd_onloss;

typedef struct {
    char *name, *program;
    uint32_t epoch, cycle;
    mfd_op op;
    ml_list members; /* rank r is members[r] */
    mfd_gstate state;
    mfd_onloss on_loss;
    uint32_t auto_ms; /* with on_loss reform: re-form after this long paused (0: only when asked) */
} mfd_group;

uint32_t mfd_group_mask(const mfd_group *g);
bool mfd_group_rank(const mfd_group *g, uint32_t k, uint32_t *rank);
/* The rank order as bytes (F_HELLO): members[r], 0xff past the end. */
void mfd_group_order(const mfd_group *g, uint8_t out[MESH_MAX_SYSTEMS]);
mfd_group mfd_group_clone(const mfd_group *g);
bool mfd_group_eq(const mfd_group *a, const mfd_group *b);
void mfd_group_free(mfd_group *g);
char *mfd_op_word(mfd_op op);
bool mfd_op_parse(const char *s, mfd_op *out, char **err);

typedef struct {
    uint64_t version, term; /* term: the leader that proposed it */
    /* programs, sorted by name */
    char **pname;
    mfd_prog *prog;
    size_t nprogs;
    mfd_group *groups;
    size_t ngroups;
    /* placement requests (`place <name> <words>`), sorted by program */
    char **place_name;
    mt_strs *place_words;
    size_t nplaces;
    bool sched_paused; /* `sched pause` */
    bool autoreform;   /* `sched autoreform on` */
    uint32_t cordon;   /* `sched cordon <k>`: no new placements on these members */
} mfd_doc;

mfd_doc mfd_doc_clone(const mfd_doc *d);
bool mfd_doc_eq(const mfd_doc *a, const mfd_doc *b);
void mfd_doc_free(mfd_doc *d);
/* (term, version): the order of documents. */
int mfd_id_cmp(uint64_t at, uint64_t av, uint64_t bt, uint64_t bv);

mfd_prog *mfd_prog_get(const mfd_doc *d, const char *name);
long mfd_prog_index(const mfd_doc *d, const char *name);
/* Inserts or replaces (takes p). */
void mfd_prog_put(mfd_doc *d, const char *name, mfd_prog p);
bool mfd_prog_remove(mfd_doc *d, const char *name);
mfd_group *mfd_group_of(const mfd_doc *d, const char *program);
mfd_group *mfd_group_named(const mfd_doc *d, const char *name);
void mfd_group_push(mfd_doc *d, mfd_group g); /* takes g */
void mfd_group_remove_at(mfd_doc *d, size_t i);
const mt_strs *mfd_place_get(const mfd_doc *d, const char *name);
void mfd_place_put(mfd_doc *d, const char *name, mt_strs words); /* takes words */
bool mfd_place_remove(mfd_doc *d, const char *name);

char *mfd_encode(const mfd_doc *d);
bool mfd_decode(const char *text, mfd_doc *out, char **err);

/* Whether a member whose GPU backend is kind (BACKEND_*) can run a program listing backends (every
 * meshd has the wasm executor; a native program is assumed). */
bool mfd_can_run(const mt_strs *backends, uint32_t kind);

#endif
