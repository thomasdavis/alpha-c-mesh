/* @ajax/mesh-backend: the executor interface meshd's driver sees (mesh-host
 * backend.rs).  Everything above it -- rings, publisher, lifecycle,
 * placement, peers -- is backend-neutral mailbox words.
 *
 * - CUDA (@ajax/mesh-mps over @ajax/mesh-cuda, Linux): one resident kernel per
 *   program, PTX compiled by nvJitLink, the mailbox in pinned host memory.
 * - wasm (@ajax/mesh-exec-wasm): CPU programs on lease lanes.
 * - native (@ajax/mesh-native): a program that is an executable runs as its own
 *   worker process on the shared-memory mailbox.
 * - Metal is not ported (Metal Shading Language is C++).
 *
 * The Rust's traits are tables of functions here: a backend, its compiler, a
 * loaded image (mesh_kernel) and where a slot's work runs (mesh_stream) each
 * carry `ops` and their implementation's `self`.  Errors are `char **err`:
 * on failure the function returns false and sets *err to a malloc'd message
 * (the Rust's text; load refusals start with a LOAD_REASONS word). */
#ifndef MESH_BACKEND_H
#define MESH_BACKEND_H

#include "mesh/loader.h"
#include "mesh/manifest.h"

#include "mesh/abi/backend.h"
#include "mesh/abi/control_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* CUDA's isolation modes (`--isolation`): one context that blinks, or MPS workers. */
typedef enum { MESH_ISOLATION_MODE_BLINK, MESH_ISOLATION_MODE_MPS } mesh_isolation_mode;

/* nvJitLink's default dir (`--nvjitlink`). */
#define MESH_NVJITLINK_DIR "/usr/local/cuda/lib64"
/* A blink scope that halts nothing (no program sits in it): PROG_SLOTS. */
#define MESH_NO_SLOT 32u

/* This machine's GPU (or executor) as the mailbox header and F_JOIN describe it. */
typedef struct {
    uint32_t kind; /* BACKEND_* */
    char *name;
    int32_t cc_major, cc_minor; /* CUDA compute capability; 0, 0 elsewhere */
    uint32_t sms_total;         /* SMs (CUDA) / cores of the device */
    uint32_t sms;               /* those programs run on */
    bool green;
    uint32_t blocks_per_sm; /* CUDA resident blocks per SM; 0 elsewhere */
    uint32_t family;        /* Metal's MTLGPUFamilyAppleN; 0 on CUDA */
    uint32_t max_tg_threads; /* the most threads a node may have (1024); 0: no GPU */
    uint32_t max_shared;     /* the most shared memory a node may have, the runtime's included; 0: no GPU */
    char *arch;              /* what code is compiled for: "sm_121" */
} mesh_caps;

uint32_t mesh_caps_word(const mesh_caps *c);              /* backend_word(kind, family) */
const char *mesh_caps_backend_name(const mesh_caps *c);   /* BACKENDS[kind] */
void mesh_caps_copy(mesh_caps *dst, const mesh_caps *src);
void mesh_caps_free(mesh_caps *c);
/* A backend label for logs and the control port: "cuda", "wasm", "metal-apple9", "backend7" (malloc'd). */
char *mesh_backend_label(uint32_t word);

/* ---- a loaded program image (Rust: backend::Kernel) ------------------------------------ */

typedef struct mesh_kernel mesh_kernel;
typedef struct {
    uint32_t exec; /* the executor that runs it (BACKEND_*) */
    /* Registers per thread (CUDA); 0 elsewhere. */
    bool (*regs)(const mesh_kernel *k, uint32_t *out, char **err);
    /* Local (stack) bytes per thread; 0 on the CPU executors. */
    bool (*local_bytes)(const mesh_kernel *k, uint32_t *out, char **err);
    /* CUDA: resident blocks of the node's shape per SM; the CPU executors: 1. */
    bool (*max_blocks_per_sm)(const mesh_kernel *k, uint32_t *out, char **err);
    /* The program's scratch: its device address (PX_SCRATCH); 0: none. */
    uint64_t (*scratch)(const mesh_kernel *k);
    /* A u32 global of the image, e.g. MESH_RT_ABI. */
    bool (*global_u32)(const mesh_kernel *k, const char *name, uint32_t *out, char **err);
    /* Drops the image without the backend's unload (Rust's Drop: an image a
     * failed load or a superseded prepare leaves behind). */
    void (*drop)(mesh_kernel *k);
} mesh_kernel_ops;

struct mesh_kernel {
    const mesh_kernel_ops *ops;
    void *self;
};

static inline uint32_t mesh_kernel_exec(const mesh_kernel *k) { return k->ops->exec; }
/* The native worker image behind k, or NULL (Rust: Kernel::native). */
static inline void *mesh_kernel_native(const mesh_kernel *k) {
    return k && k->ops->exec == MESH_BACKEND_NATIVE ? k->self : NULL;
}
bool mesh_kernel_regs(const mesh_kernel *k, uint32_t *out, char **err);
bool mesh_kernel_local_bytes(const mesh_kernel *k, uint32_t *out, char **err);
bool mesh_kernel_max_blocks_per_sm(const mesh_kernel *k, uint32_t *out, char **err);
uint64_t mesh_kernel_scratch(const mesh_kernel *k);
bool mesh_kernel_global_u32(const mesh_kernel *k, const char *name, uint32_t *out, char **err);
/* Drops and frees a heap-allocated kernel (NULL: nothing). */
void mesh_kernel_drop(mesh_kernel *k);

/* ---- where a slot's work runs (Rust: backend::Stream) --------------------------------- */

typedef struct mesh_stream {
    uint32_t exec; /* BACKEND_* */
    uint32_t slot;
    void *self;    /* the backend's (a CUDA stream, ...); NULL for the CPU executors */
    void (*drop)(struct mesh_stream *s); /* NULL: nothing to release */
} mesh_stream;

/* Releases the stream's resources and frees it (NULL: nothing). */
void mesh_stream_drop(mesh_stream *s);

/* ---- a compiled image, ready for the blink (Rust: backend::Prepared) ------------------ */

typedef struct {
    mesh_manifest manifest;
    /* The image's identity in the program table and on the peer link. */
    uint64_t hash;
    /* CUDA: the cubin; native: the binary. */
    uint8_t *code;
    size_t code_len;
    double compile_ms;
    /* The bytes it came from (replicated to the peers). */
    mesh_payload src;
    /* mps: its worker, started with the module loaded; native: the worker
     * image; wasm: the compiled module.  NULL: the blink loads the code. */
    mesh_kernel *kernel;
    /* The executor that compiled it (BACKEND_*): the one that runs it. */
    uint32_t exec;
    /* CUDA: the register cap it was compiled with (PX_REG_CAP); 0: none. */
    uint32_t reg_cap;
} mesh_prepared;

static inline const char *mesh_prepared_dir(const mesh_prepared *p) { return p->src.dir; }
/* Frees everything, dropping a kernel still held. */
void mesh_prepared_free(mesh_prepared *p);

/* ---- compilers (Rust: trait Compiler) -------------------------------------------------- */

typedef struct mesh_compiler mesh_compiler;
typedef struct {
    /* Whether the catalog must hold the PTX (CUDA compiles it). */
    bool (*needs_ptx)(const mesh_compiler *c);
    /* Manifest checks, the hash (checked against *expect, a peer's F_LOAD;
     * expect NULL: none), compile.  root is --programs.  Takes ownership of
     * *src (it moves into out->src on success, is freed on failure).  Errors
     * start with a LOAD_REASONS word.  Runs on the JIT thread: thread-safe. */
    bool (*prepare)(const mesh_compiler *c, mesh_payload *src, const uint64_t *expect, const char *root, mesh_prepared *out,
                    char **err);
} mesh_compiler_ops;

struct mesh_compiler {
    const mesh_compiler_ops *ops;
    void *self;
};

/* ---- the backend (Rust: trait Backend) ------------------------------------------------- */

typedef struct mesh_backend mesh_backend;
typedef struct {
    const mesh_caps *(*caps)(const mesh_backend *b);
    /* A module change halts only the slot it changes (mps, the CPU executors);
     * false: every program of the context blinks (CUDA's default). */
    bool (*isolated)(const mesh_backend *b);
    /* Programs are resident kernels sharing the device's block slots: the occupancy budget applies (CUDA). */
    bool (*resident)(const mesh_backend *b);
    /* S_ISOLATION (ISOLATION_*). */
    uint32_t (*isolation)(const mesh_backend *b);
    /* The mailbox: its host address and the device address the kernels get. */
    bool (*mailbox)(const mesh_backend *b, uint8_t **host, uint64_t *dev, char **err);
    bool (*make_current)(const mesh_backend *b, char **err);
    /* The compiler (owned by the backend, valid while it lives). */
    const mesh_compiler *(*compiler)(const mesh_backend *b);
    /* The image's module (blink: loaded here; mps, native, wasm: prepared
     * already, taken out of p->kernel).  *out is a new heap kernel. */
    bool (*load)(const mesh_backend *b, mesh_prepared *p, mesh_kernel **out, char **err);
    bool (*new_stream)(const mesh_backend *b, uint32_t slot, const mesh_kernel *k, mesh_stream **out, char **err);
    /* Starts slot's nodes (returns at once; P_ONLINE tells when they are up). */
    bool (*launch)(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, uint32_t nodes, uint64_t dev,
                   uint32_t slot, char **err);
    /* *done = true once the slot's last launch finished. */
    bool (*query)(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k, bool *done, char **err);
    /* Unloads and frees k (whatever happens). */
    bool (*unload)(const mesh_backend *b, mesh_kernel *k, char **err);
    bool (*stack_limit)(const mesh_backend *b, size_t *out, char **err);
    bool (*set_stack_limit)(const mesh_backend *b, size_t bytes, char **err);
    /* The halt scope of a module change of slot (has_slot false: a load or a
     * free).  Returns true with *scope set, or false for None (the whole context). */
    bool (*module_scope)(const mesh_backend *b, bool has_slot, uint32_t slot, uint32_t *scope);
    /* The slot's work ended abnormally (a native worker died): why (malloc'd), once; NULL: nothing. NULL op: never. */
    char *(*crashed)(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k);
    /* The slot's last launch is over by the executor's own account (a native worker). NULL op: false. */
    bool (*owns_halt)(const mesh_backend *b);
    /* Running nodes re-read their reads mask at every peer view. NULL op: false. */
    bool (*reads_live)(const mesh_backend *b);
    /* About once a second: refresh what the executor measures. NULL op: nothing. */
    void (*sample)(const mesh_backend *b);
    /* A launch's nodes must be online within this many ms. NULL op: 2000. */
    uint32_t (*online_timeout_ms)(const mesh_backend *b);
    /* Frees the backend (its exit paths have run cleanup already). */
    void (*destroy)(mesh_backend *b);
} mesh_backend_ops;

struct mesh_backend {
    const mesh_backend_ops *ops;
    void *self;
};

/* The trait's default methods, applied when an op is NULL. */
char *mesh_backend_crashed(const mesh_backend *b, const mesh_stream *st, const mesh_kernel *k);
bool mesh_backend_owns_halt(const mesh_backend *b);
bool mesh_backend_reads_live(const mesh_backend *b);
void mesh_backend_sample(const mesh_backend *b);
uint32_t mesh_backend_online_timeout_ms(const mesh_backend *b);
void mesh_backend_destroy(mesh_backend *b);

/* ---- free functions of backend.rs ------------------------------------------------------- */

/* The executor for a manifest: the first of its `backends` this meshd runs
 * (have: BACKEND_* kinds, the GPU's first).  False with *err ("unsupported: ..."). */
bool mesh_pick_exec(const mesh_manifest *m, const uint32_t *have, size_t nhave, uint32_t *exec, char **err);
/* Program m in catalog dir has an image for one of have: the executor
 * pick_exec names, and for a native program its binary for this target. */
bool mesh_runs_here(const char *dir, const mesh_manifest *m, const uint32_t *have, size_t nhave);

/* The executors' compilers, the GPU's first (what the JIT thread gets). */
typedef struct {
    uint32_t kind;
    const mesh_compiler *comp;
} mesh_compiler_entry;

/* Picks the executor for the payload's manifest and prepares the image with
 * its compiler (on the JIT thread).  Takes ownership of *src. */
bool mesh_prepare_any(const mesh_compiler_entry *comps, size_t n, mesh_payload *src, const uint64_t *expect, const char *root,
                      mesh_prepared *out, char **err);
/* The manifest and abi checks every backend makes first, including
 * `backends`: a program that does not list this backend is refused as `unsupported`. */
bool mesh_check_manifest(const mesh_payload *src, uint32_t kind, mesh_manifest *out, char **err);

/* Every exit path calls it: native workers first, then the CUDA MPS daemon
 * and workers (as backend.rs).  Executors register their cleanup when they
 * start; cleanups run in ascending `order` (MESH_CLEANUP_NATIVE before
 * MESH_CLEANUP_MPS), each once per registration. */
#define MESH_CLEANUP_NATIVE 10
#define MESH_CLEANUP_WASM 15
#define MESH_CLEANUP_MPS 20
void mesh_backend_cleanup(void);
void mesh_backend_on_cleanup(int order, void (*fn)(void));

/* The executable a native program has for this target: <dir>/<target triple>/<name> (malloc'd). */
char *mesh_native_binary(const char *dir, const char *name);
/* The target triple this was built for ("x86_64-unknown-linux-gnu"). */
const char *mesh_native_target(void);

#endif
