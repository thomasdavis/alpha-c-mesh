/* @ajax/mesh-native: native worker processes (mesh-host native.rs).  A
 * program whose manifest says `backends = ["native"]` is an executable built
 * for this machine's target (`<catalog dir>/<target triple>/<name>`, linking
 * @ajax/mesh-worker).  It runs as its own process beside the GPU backend,
 * maps meshd's mailbox and plays mesh-rt's node loop on its slot's ring.
 *
 * An image (mesh_native_worker, a mesh_kernel of exec BACKEND_NATIVE) owns at
 * most one process at a time:
 * - prepare (the JIT thread): manifest, the binary for this target, the image
 *   hash (manifest + source.hash), the limits' availability;
 * - launch: `run <gen> <slot>` to that process, or a new one is spawned off
 *   the driver thread (a start after a stop) -- P_ONLINE says when it is up;
 * - P_SHUTDOWN = SHUTDOWN_HALT (a blink, an upgrade, rollback, rebind): the
 *   nodes halt, the process stays and waits for the next `run`;
 * - P_SHUTDOWN = SHUTDOWN_EXIT (stop, unload, FAILED, meshd's exit): the
 *   process halts and exits; it is reaped, and killed after EXIT_GRACE;
 * - a process that dies while it runs fails the slot (FAILED) with its exit
 *   status and the tail of its output; meshd and every other program go on. */
#ifndef MESH_NATIVE_H
#define MESH_NATIVE_H

#include "mesh/backend.h"
#include "mesh/native_limits.h"
#include "mesh/shm.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* Spawn to `ready`, the program's own setup included. */
#define MESH_NATIVE_READY_MS 60000u
/* A launch whose process is not up by then fails (the driver's ONLINE for native slots). */
#define MESH_NATIVE_ONLINE_MS 60000u
/* A worker told to exit is killed when it is still there this long after. */
#define MESH_NATIVE_EXIT_GRACE_MS 3000u
/* Output lines kept per process for a crash report. */
#define MESH_NATIVE_TAIL 30

/* The executable format of this target: an ELF of this machine (a refusal names the file). */
bool mesh_native_check_format(const char *name, const char *path, const uint8_t *b, size_t n, char **err);

/* How workers that use the GPU (manifest `device = "gpu"`) reach it. */
typedef enum {
    MESH_GPU_ACCESS_NONE,  /* no GPU (--backend wasm): refused at load */
    MESH_GPU_ACCESS_BLINK, /* CUDA in blink mode: refused at load (time-sliced against the resident kernels) */
    MESH_GPU_ACCESS_MPS,   /* CUDA under --isolation mps: a client of meshd's server, checked after `ready` */
    MESH_GPU_ACCESS_METAL,
} mesh_gpu_access_kind;

/* What every worker of this meshd is started with. */
typedef struct {
    char **env; /* extra "K=V" entries (MESH_SERVICE_BASE, MESH_MODELS, MESH_SYSTEM), NULL-terminated or NULL */
    mesh_gpu_access_kind gpu;
    char *mps_pipe_dir; /* MPS: CUDA_MPS_PIPE_DIRECTORY for GPU workers */
    /* MPS: checks that a GPU worker that said `ready` is a client of meshd's server
     * (@ajax/mesh-mps's mesh_mps_check_native); NULL: none. */
    bool (*check_gpu_client)(pid_t pid, char **err);
} mesh_worker_cfg;

/* Shared by every native image of a meshd: the mailbox every worker maps, where logs go. */
typedef struct mesh_native_host mesh_native_host;

/* logs: each program's output goes to <logs>/<name>.log.  cfg is copied. */
mesh_native_host *mesh_native_host_new(const mesh_shm *shm, const char *logs, const mesh_worker_cfg *cfg);

/* Machine totals for the host block. */
typedef struct {
    uint32_t procs;
    float cpu_pct;
    uint64_t rss;
    uint32_t spawns, crashes;
} mesh_native_totals;

/* Live processes, spawns and crashes now (no sampling). */
void mesh_native_host_counts(mesh_native_host *h, uint32_t *live, uint32_t *spawns, uint32_t *crashes);
/* Samples every live worker: the machine's totals (about once a second). */
mesh_native_totals mesh_native_host_totals(mesh_native_host *h);

/* One image's numbers for its slot's control words. */
typedef struct {
    pid_t pid;
    float cpu_pct;
    uint64_t rss, cpu_ns;
    uint32_t spawns, crashes, exit, limits;
} mesh_native_stats;

/* A native program image (behind a mesh_kernel). */
typedef struct mesh_native_worker mesh_native_worker;

const char *mesh_native_worker_name(const mesh_native_worker *w);
const char *mesh_native_worker_bin(const mesh_native_worker *w);
uint64_t mesh_native_worker_bin_hash(const mesh_native_worker *w);
uint64_t mesh_native_worker_bin_bytes(const mesh_native_worker *w);
/* Starts its process now and waits for `ready` (the JIT thread). */
bool mesh_native_worker_warm(mesh_native_worker *w, char **err);
/* Launch on slot: `run` to its process, or a new one spawned off this thread. */
bool mesh_native_worker_launch(mesh_native_worker *w, uint32_t slot, char **err);
/* The last launch is over (halted, or its process gone). */
bool mesh_native_worker_query(mesh_native_worker *w);
/* A failure of the running launch, reported once (malloc'd), or NULL. */
char *mesh_native_worker_crashed(mesh_native_worker *w);
/* Its numbers (call after mesh_native_host_totals, which samples every process). */
mesh_native_stats mesh_native_worker_stats(mesh_native_worker *w);
/* Unload: its process (if any) exits, reaped off this thread. */
void mesh_native_worker_release(mesh_native_worker *w);
/* The pid of its live process, 0 if none. */
pid_t mesh_native_worker_pid(mesh_native_worker *w);

/* The native executor beside the GPU backend: isolated, not resident.  The
 * backend owns the host.  Registers mesh_native_cleanup with mesh-backend. */
mesh_backend *mesh_native_backend_new(const mesh_shm *shm, const char *logs, const mesh_worker_cfg *cfg);
mesh_native_host *mesh_native_backend_host(const mesh_backend *b);
/* Every worker of every host: asked to exit, then killed and reaped (meshd's exit paths). */
void mesh_native_cleanup(void);

#endif
