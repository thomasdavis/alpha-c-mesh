/* @ajax/mesh-driver: meshd's driver thread (driver.rs).
 *
 * The only publisher of every ring and the only writer of the program table.
 * Each tick it collects completions, honours run requests and commands, and
 * publishes the next tasks of every loaded program -- each its own resident
 * kernel (or wasm lanes, or a native worker) with its own ring.
 *
 * Lifecycle (plan §3.1): start/stop launch or halt one program's kernel and
 * touch nothing else (zero gap). Anything that changes the context's module
 * set (load, upgrade, unload --now) is a *blink*, because module loads and
 * unloads block while any kernel of the context runs: drain every ring, halt
 * every kernel, do the module operations (images compiled beforehand on a
 * worker thread), rewind each ring head to its published tail, launch
 * everything that runs, then wait for all of it. Rollback and reset need no
 * module operation: they blink only their slot. Isolated executors (mps, the
 * CPU executors) blink only the slot a module change touches.
 *
 * Executors are per slot (mesh-abi PX_EXEC): the GPU backend and beside it the
 * CPU executors (wasm lanes, native worker processes). A load runs on the
 * first executor its manifest's `backends` lists that this meshd has. */
#ifndef MESH_DRIVER_H
#define MESH_DRIVER_H

#include "mesh/backend.h"
#include "mesh/shared.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool peered;
    const char *state; /* --state FILE, or NULL */
    bool autoload;
    /* A fleet member with a committed desired state: no --state / --autoload loads. */
    bool skip_startup;
} mesh_driver_config;

typedef struct mesh_driver mesh_driver;

/* A native worker's counts and costs (the driver copies them into its slot's
 * control block and the CPU meter every tick). */
typedef struct {
    uint32_t pid, spawns, crashes, exit, limits;
    uint64_t cpu_ns, rss;
} mesh_native_stats;

/* The native executor's view of a worker image (set by meshd when the native
 * executor is wired; NULL: no native slot reports anything). */
extern bool (*mesh_driver_native_stats)(const mesh_kernel *k, mesh_native_stats *out);
/* Starts a native image's process off the driver thread before its first run (NULL: none). */
extern bool (*mesh_driver_native_warm)(const mesh_kernel *k, char **err);

/* gpu: the GPU backend (or wasm itself: a CPU-only meshd); cpus: the CPU
 * executors beside it. The driver owns the backends. */
mesh_driver *mesh_driver_new(mesh_shared *sh, mesh_backend *gpu, mesh_backend **cpus, size_t ncpus, mesh_driver_config cfg);
/* make_current, then the startup loads (--state, --autoload): false with *err. */
bool mesh_driver_start(mesh_driver *d, char **err);
/* The driver loop; returns the process exit code after SIGINT/SIGTERM (sh->quit). */
int mesh_driver_run(mesh_driver *d);
void mesh_driver_free(mesh_driver *d);

/* Bit s set for every loaded name the manifest's `reads` lists ("*": READS_ALL). Exposed for tests. */
uint32_t mesh_reads_mask(const mesh_manifest *m, const char *const *names, size_t n);

/* A job's setup, one task at a time, in order (driver.rs SetupState). Exposed for tests. */
typedef struct {
    bool has_job;
    uint64_t job;
    size_t next;
    bool busy;
    char *err;
} mesh_setup_state;

typedef enum { MESH_SETUP_PUBLISH, MESH_SETUP_WAIT, MESH_SETUP_FAILED, MESH_SETUP_READY } mesh_setup_step_kind;

typedef struct {
    mesh_setup_step_kind k;
    uint32_t op, arg;  /* PUBLISH */
    const char *err;   /* FAILED (the state's) */
} mesh_setup_step;

mesh_setup_step mesh_setup_state_step(mesh_setup_state *s, uint64_t job, const mj_setup *setup, size_t n);
void mesh_setup_state_published(mesh_setup_state *s);
void mesh_setup_state_completed(mesh_setup_state *s, uint64_t job, const char *err);
void mesh_setup_state_reset(mesh_setup_state *s);

#endif
