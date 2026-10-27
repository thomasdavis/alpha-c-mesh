/* @ajax/mesh-cuda: the CUDA driver calls meshd (and kcheck, and the MPS
 * workers) need (mesh-host cuda.rs): a primary or green context, streams (one
 * per program), the pinned mailbox, and loading (cubins only: nvJitLink
 * compiles, never the driver) / launching / unloading resident kernels.
 *
 * Module loading is EAGER, always: under LAZY (the driver default) a kernel's
 * first launch is itself a module load, which blocks behind every resident
 * kernel of the context.  mesh_cuda_force_eager runs before cuInit. */
#ifndef MESH_CUDA_H
#define MESH_CUDA_H

#include "mesh/cuda_driver.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_CUDA_MODULE_LOADING "CUDA_MODULE_LOADING"
/* The per-machine CUDA driver library dir (a machine whose kernel module and
 * userland libcuda differ keeps a matching private libcuda.so.1 there). */
#define MESH_CUDA_LIB_ENV "MESH_CUDA_LIB"

/* Sets CUDA_MODULE_LOADING=EAGER, or refuses a different setting.  Call it
 * first in main, before any thread exists. */
bool mesh_cuda_force_eager(char **err);
/* `--cuda-lib DIR` from argv, else $MESH_CUDA_LIB; NULL: the system libcuda (malloc'd). */
char *mesh_cuda_lib_dir(int argc, char **argv);
/* Makes this process (and every child) load libcuda.so.1 from dir: when dir is
 * not LD_LIBRARY_PATH's first entry yet, re-executes this program with dir
 * prepended (and does not return).  Call first in main.  dir NULL: nothing. */
bool mesh_cuda_use_lib(const char *dir, int argc, char **argv, char **err);
/* After cuInit: the libcuda this process mapped (malloc'd); with
 * MESH_CUDA_LIB set it must be the one in that dir. */
char *mesh_cuda_check_lib(char **err);

/* The device's per-block limits a multi-warp node must fit. */
typedef struct {
    uint32_t max_threads;    /* MAX_THREADS_PER_BLOCK (1024) */
    uint32_t regs_per_block; /* MAX_REGISTERS_PER_BLOCK (65536) */
    uint32_t max_shared;     /* MAX_SHARED_MEMORY_PER_BLOCK_OPTIN (sm_86 and GB10: 99 KiB) */
} mesh_cuda_limits;

bool mesh_cuda_limits_of(CUdevice dev, mesh_cuda_limits *out, char **err);
/* The register cap nvJitLink must compile a node of `warps` warps with so
 * that one block fits the register file: 0 when 255 per thread fit already. */
uint32_t mesh_cuda_reg_cap(const mesh_cuda_limits *l, uint32_t warps);
/* A node of warps warps and smem dynamic shared bytes fits a block here. */
bool mesh_cuda_limits_check(const mesh_cuda_limits *l, const char *name, uint32_t warps, uint32_t smem, char **err);

typedef struct {
    CUcontext ctx;
    CUgreenCtx green_ctx; /* the green context behind ctx, if any */
    char *name;
    int32_t cc_major, cc_minor;
    uint32_t sms_total;
    uint32_t sms; /* SMs the mesh may use: the green context's, else all */
    bool green;
    uint32_t blocks_per_sm; /* the resident-block budget per SM every program's nodes share */
    bool host_atomics;      /* HOST_NATIVE_ATOMIC_SUPPORTED (0 on an RTX 3060 over PCIe) */
    mesh_cuda_limits limits;
} mesh_cuda_gpu;

/* A loaded kernel module and its entry, with the launch shape of its nodes. */
typedef struct {
    CUmodule module;
    CUfunction func;
    uint32_t threads; /* per block (32 x warps) */
    uint32_t smem;    /* dynamic shared bytes per block (RT_SHARED_BYTES + shared_bytes) */
    uint64_t scratch; /* the program's scratch, zeroed at load; 0: none */
} mesh_cuda_kernel;

/* A non-blocking stream of the context (one per program). */
typedef struct {
    CUstream s;
} mesh_cuda_stream;

/* Device 0; sms > 0: a green context of that many SMs.  Needs
 * CUDA_MODULE_LOADING=EAGER (mesh_cuda_force_eager first). */
bool mesh_cuda_gpu_open(uint32_t sms, mesh_cuda_gpu *out, char **err);
void mesh_cuda_gpu_free(mesh_cuda_gpu *g);
bool mesh_cuda_make_current(const mesh_cuda_gpu *g, char **err);
/* The per-thread stack the context reserves (raise it only while nothing runs: in a blink). */
bool mesh_cuda_stack_limit(const mesh_cuda_gpu *g, size_t *out, char **err);
bool mesh_cuda_set_stack_limit(const mesh_cuda_gpu *g, size_t bytes, char **err);
/* Host memory (the mailbox's shared file) pinned and mapped into the device: its device address. */
bool mesh_cuda_host_register(const mesh_cuda_gpu *g, void *p, size_t bytes, uint64_t *dev, char **err);
/* A cubin and its entry (quick; but like every module operation it blocks while any kernel runs). */
bool mesh_cuda_load_cubin(const mesh_cuda_gpu *g, const uint8_t *cubin, const char *entry, mesh_cuda_kernel *out, char **err);
bool mesh_cuda_new_stream(const mesh_cuda_gpu *g, mesh_cuda_stream *out, char **err);
void mesh_cuda_stream_destroy(mesh_cuda_stream *s);
/* nodes blocks of k's shape on st; params point at the kernel's parameters. Returns at once. */
bool mesh_cuda_launch_on(const mesh_cuda_gpu *g, const mesh_cuda_stream *st, const mesh_cuda_kernel *k, uint32_t nodes,
                         void **params, char **err);
/* *done = true once everything on st finished. */
bool mesh_cuda_query(const mesh_cuda_gpu *g, const mesh_cuda_stream *st, bool *done, char **err);
/* The module and the scratch (both block while a kernel of the context runs: call it halted). */
bool mesh_cuda_unload(const mesh_cuda_gpu *g, mesh_cuda_kernel *k, char **err);

bool mesh_cuda_kernel_regs(const mesh_cuda_kernel *k, uint32_t *out, char **err);
bool mesh_cuda_kernel_local_bytes(const mesh_cuda_kernel *k, uint32_t *out, char **err);
/* Sets the launch shape, opting the entry in above 48 KiB of shared memory;
 * refuses a shape the compiled entry cannot launch with. */
bool mesh_cuda_kernel_set_shape(mesh_cuda_kernel *k, uint32_t threads, uint32_t smem, char **err);
/* bytes of device memory, zeroed before this returns (nothing when 0). */
bool mesh_cuda_kernel_alloc_scratch(mesh_cuda_kernel *k, uint32_t bytes, char **err);
bool mesh_cuda_kernel_max_blocks_per_sm(const mesh_cuda_kernel *k, uint32_t *out, char **err);
/* A u32 module global (MESH_RT_ABI): a synchronous copy, call it while no kernel runs. */
bool mesh_cuda_kernel_global_u32(const mesh_cuda_kernel *k, const char *name, uint32_t *out, char **err);

#endif
