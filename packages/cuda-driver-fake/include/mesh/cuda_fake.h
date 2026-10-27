/* @ajax/cuda-driver-fake: a simulated CUDA device behind @ajax/cuda-driver's
 * table, so everything above the driver -- contexts, green contexts, module
 * loads, launch shapes, streams, the mailbox registration, the blink, MPS
 * workers -- is testable on a machine without a GPU.
 *
 * Device memory is host memory (a device pointer is the host address), so a
 * launched kernel can be a C function that plays the program's node loop on
 * CPU threads against the real mailbox.  A "cubin" is text:
 *   "FAKECUBIN entry=mesh_main regs=40 local=0 maxthreads=1024 shared=0 rt_abi=6"
 * (any key may be left out).  Not thread-safe across tests: install once. */
#ifndef MESH_CUDA_FAKE_H
#define MESH_CUDA_FAKE_H

#include "mesh/cuda_driver.h"

#include <stdbool.h>
#include <stdint.h>

/* Runs one launch: nodes blocks of threads threads, the kernel's parameters
 * (a mesh program: params[0] -> u64 mailbox device address, params[1] -> u32
 * slot, copied).  Called on a thread of its own; the stream is busy until it returns. */
typedef void (*mesh_fake_kernel_fn)(const char *entry, uint64_t mailbox, uint32_t slot, uint32_t nodes, uint32_t threads,
                                    void *user);

typedef struct {
    const char *name;           /* "Fake GB10" */
    int cc_major, cc_minor;     /* 12, 1 */
    int sms;                    /* 48 */
    int blocks_per_sm;          /* 24 */
    int host_atomics;           /* 1 */
    int max_threads;            /* 1024 */
    int regs_per_block;         /* 65536 */
    int max_shared_optin;       /* 101376 */
    int loading_mode;           /* CU_MODULE_EAGER_LOADING */
    int blocks_per_sm_occupancy; /* what cuOccupancyMaxActiveBlocksPerMultiprocessor answers; 0: 16 */
    mesh_fake_kernel_fn run;
    void *user;
} mesh_fake_cuda_cfg;

/* What the fake saw. */
typedef struct {
    int inits, primary_ctx, green_ctx, green_sms;
    int modules_loaded, modules_unloaded, modules_live;
    int streams_created, streams_destroyed;
    int launches, running;
    uint32_t last_grid, last_block, last_smem;
    uint64_t last_mailbox;
    uint32_t last_slot;
    int host_registered;
    int allocs, frees, memsets;
    size_t stack_limit;
    int opted_in_smem; /* MAX_DYNAMIC_SHARED_SIZE_BYTES set */
} mesh_fake_cuda_stats;

/* Installs the fake as the driver table (cfg fields left 0 take the defaults above). */
void mesh_fake_cuda_install(const mesh_fake_cuda_cfg *cfg);
mesh_fake_cuda_stats mesh_fake_cuda_stats_now(void);
/* The next call to `call` (e.g. "cuModuleLoadData") returns code, once. */
void mesh_fake_cuda_fail_next(const char *call, CUresult code);
/* Waits (at most ms) until no launch is running; false if one still is. */
bool mesh_fake_cuda_idle(unsigned ms);

#endif
