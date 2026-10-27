/* @ajax/mesh-fleet-plan: the reconcile plan (fleet.rs plan) and a fresh fleet's first document
 * (seed). */
#ifndef MESH_FLEET_PLAN_H
#define MESH_FLEET_PLAN_H

#include "mesh/fleet_doc.h"

#include <stdbool.h>
#include <stdint.h>

/* A program as it is on this member (the driver's inventory). */
typedef struct {
    char *name, *dir;
    uint64_t hash;
    bool has_prev;     /* the retained previous image's hash (rollback target) */
    uint64_t prev;
    bool run, failed, lockstep;
} mfl_actual;

mfl_actual mfl_actual_new(const char *name, const char *dir, uint64_t hash, bool has_prev, uint64_t prev, bool run);
void mfl_actual_free(mfl_actual *a);

typedef enum {
    MFL_UNLOAD,
    MFL_LOAD,     /* name, dir, hash, run */
    MFL_UPGRADE,  /* name, dir, hash */
    MFL_ROLLBACK,
    MFL_RUN,      /* name, run */
    MFL_RESET,
    MFL_MIGRATE,  /* load it stopped, take its view from member `from` (which stops it), then run it */
} mfl_kind;

typedef struct {
    mfl_kind kind;
    char *name, *dir;
    uint64_t hash;
    bool run;
    uint32_t from;
} mfl_action;

typedef struct {
    mfl_action *v;
    size_t n;
} mfl_actions;

bool mfl_action_eq(const mfl_action *a, const mfl_action *b);
void mfl_actions_free(mfl_actions *a);

/* skip(name, hash): programs this member does not run (no image for its executors) or already
 * failed at this hash. resets: the reset generation applied per program (name -> decimal u32). */
typedef bool (*mfl_skip_fn)(void *ctx, const char *name, uint64_t hash);

/* What makes this member's programs match doc: unloads first (they free slots and heap), then loads,
 * upgrades and rollbacks, then run states and resets. A failed program that should run is started
 * again. resets maps a program to its applied generation (as text). */
mfl_actions mfl_plan(const mfd_doc *doc, const mfl_actual *actual, size_t nactual, mfl_skip_fn skip, void *ctx, const char *const *reset_names,
                     const uint32_t *reset_gens, size_t nresets, uint32_t me);

/* The first document of a fresh fleet: the first leader's programs (each with its spec) and a group
 * of every eligible up member for the lockstep program. backend_of(k) is member k's GPU backend kind
 * (u32 max: unknown). */
typedef uint32_t (*mfl_backend_fn)(void *ctx, uint32_t k);
mfd_doc mfl_seed(char *const *names, const mfd_prog *specs, size_t n, const ml_list *up, mfl_backend_fn backend_of, void *ctx);

#endif
