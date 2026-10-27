/* @ajax/mesh-mps: meshd's CUDA backend (mesh-host mps.rs).
 *
 * Blink mode (the default): every program is a resident kernel of meshd's
 * one context (@ajax/mesh-cuda); a load, upgrade or unload blinks them all.
 *
 * `--isolation mps`: loads that halt no other program.  meshd starts and
 * owns a private MPS control daemon (pipe and log dirs under --mps-dir, mode
 * 0700; quit at exit).  The mailbox is the shared-memory file every worker
 * maps and registers with DEVICEMAP.  Each loaded image runs in its own
 * worker process (`meshd worker`, @ajax/mesh-mps-worker): an MPS client that
 * loads only its module and launches mesh_main on its own stream.  A module
 * load in one MPS client does not wait for the others' kernels, so the
 * worker is started and its cubin loaded on the JIT thread while everything
 * runs.  Creating an MPS client stalls the others once, so a pool of idle
 * workers is started before any program runs.  There is no fallback between
 * the modes: no daemon, a worker that is not a client of our MPS server, or
 * a failed register refuses. */
#ifndef MESH_MPS_H
#define MESH_MPS_H

#include "mesh/backend.h"
#include "mesh/cuda.h"
#include "mesh/nvjitlink.h"
#include "mesh/shm.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define MESH_MPS_PIPE_ENV "CUDA_MPS_PIPE_DIRECTORY"
#define MESH_MPS_LOG_ENV "CUDA_MPS_LOG_DIRECTORY"
#define MESH_MPS_CONTROL "nvidia-cuda-mps-control"
/* A worker's first load also starts the MPS server (context creation). */
#define MESH_MPS_WORKER_LOAD_MS 30000u
#define MESH_MPS_WORKER_LAUNCH_MS 2000u

/* The CUDA backend.  iso: blink or mps; sms > 0: a green context of that
 * many SMs (per worker in mps mode); mps_dir NULL: /tmp/meshd-mps.<pid>
 * (removed at exit); pool: mps workers started before any program runs;
 * shm: the mailbox (registered in the context, or by each worker); jit: the
 * compiler (taken).  Registers the MPS cleanup with mesh-backend. */
bool mesh_cuda_backend_open(mesh_isolation_mode iso, uint32_t sms, const char *mps_dir, uint32_t pool, const mesh_shm *shm,
                            mesh_nvjitlink *jit, mesh_backend **out, char **err);
/* Blink mode: the CUDA context (kcheck's module-load probe); NULL in mps mode. */
CUcontext mesh_cuda_backend_context(const mesh_backend *b);
bool mesh_cuda_backend_mps(const mesh_backend *b);
/* Host-native atomics (HOST_NATIVE_ATOMIC_SUPPORTED) of the device. */
bool mesh_cuda_backend_host_atomics(const mesh_backend *b);

/* Under --isolation mps: the pipe dir a native GPU worker must be started
 * with to be a client of meshd's server (malloc'd); NULL in blink mode. */
char *mesh_mps_native_pipe_dir(void);
/* A native GPU worker that said `ready` must be a client of our server (the
 * driver silently runs a process that cannot reach it as a plain context).
 * Fits mesh_worker_cfg.check_gpu_client. */
bool mesh_mps_check_native(pid_t pid, char **err);
/* Every worker killed, then the daemon quit; waits for both.  A no-op in blink mode. */
void mesh_mps_cleanup(void);

#endif
