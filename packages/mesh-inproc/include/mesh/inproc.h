/* @ajax/mesh-inproc: a GPU program driven in-process (mesh-host inproc.rs):
 * this process plays meshd's loader and publisher for one or more images,
 * through meshd's backend -- compile, load, install a program-table slot in
 * a mailbox of its own, launch, publish through the slot's ring, stop and
 * relaunch.  mesh-kcheck checks every program this way; the gpt2 worker runs
 * gpt2x's exact kernels this way. */
#ifndef MESH_INPROC_H
#define MESH_INPROC_H

#include "mesh/backend.h"
#include "mesh/manifest.h"
#include "mesh/shm.h"

#include "mesh/abi/words.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

double mesh_ms_since(uint64_t t0_ns);
uint64_t mesh_inproc_now_ns(void);

/* meshd's backend, as kcheck drives it. */
typedef struct {
    mesh_backend *b; /* owned */
    const mesh_compiler *comp;
    uint32_t kind; /* BACKEND_* */
    void *ctx;     /* CUDA: the context (the module-load probe makes it current elsewhere); NULL elsewhere */
    mesh_shm shm;  /* the mailbox the backend wraps */
} mesh_dev;

/* CUDA in blink mode (every program resident at once) over a shared-memory
 * mailbox of its own, nvJitLink from MESH_NVJITLINK_DIR. */
bool mesh_dev_open_cuda(mesh_dev *out, char **err);
/* Any other backend (wasm: @ajax/mesh-exec-wasm's), already opened over shm; takes b. */
void mesh_dev_from(mesh_backend *b, const mesh_shm *shm, mesh_dev *out);
void mesh_dev_close(mesh_dev *d);
static inline bool mesh_dev_cuda(const mesh_dev *d) { return d->kind == MESH_BACKEND_CUDA; }
static inline bool mesh_dev_wasm(const mesh_dev *d) { return d->kind == MESH_BACKEND_WASM; }
/* What the backend compiles an image to: "cubin", "MSL", "wasm". */
const char *mesh_dev_code(const mesh_dev *d);
uint32_t mesh_dev_sms_total(const mesh_dev *d);

/* A mailbox (the backend's: pinned and device-mapped; zeroed) with word access. */
typedef struct {
    uint8_t *base;
    uint64_t dev;
} mesh_mb2;

/* The backend's mailbox, with the header written (magic, version, peer none, SMs, backend). */
bool mesh_mb2_alloc(const mesh_dev *d, mesh_mb2 *out, char **err);
uint32_t mesh_mb2_r32(mesh_mb2 m, size_t off);     /* acquire */
void mesh_mb2_w32(mesh_mb2 m, size_t off, uint32_t v); /* relaxed */
void mesh_mb2_store32(mesh_mb2 m, size_t off, uint32_t v); /* release */
uint32_t mesh_mb2_add32(mesh_mb2 m, size_t off, uint32_t v); /* AcqRel, the old value */
uint64_t mesh_mb2_r64(mesh_mb2 m, size_t off);
void mesh_mb2_w64(mesh_mb2 m, size_t off, uint64_t v);
void mesh_mb2_store64(mesh_mb2 m, size_t off, uint64_t v);
float mesh_mb2_rf(mesh_mb2 m, size_t off);
void mesh_mb2_wf(mesh_mb2 m, size_t off, float v);
void mesh_mb2_bytes(mesh_mb2 m, size_t off, uint8_t *out, size_t n); /* volatile byte reads */
void mesh_mb2_write(mesh_mb2 m, size_t off, const uint8_t *b, size_t n);
void mesh_mb2_zero(mesh_mb2 m, size_t off, size_t n);
/* Pipes (mesh-abi pipe) from this side of the mailbox. */
mesh_words mesh_mb2_words(mesh_mb2 m);

/* A program image, compiled and loaded (not yet in a slot). */
typedef struct {
    mesh_manifest manifest;
    mesh_kernel *kernel;
    uint64_t hash;
    uint32_t cubin_bytes, regs, rt_abi;
    double jit_ms, load_ms;
} mesh_image2;

/* The backend's compiler on a catalog dir's bytes, then its load and the
 * loader's checks (abi, MESH_RT_ABI).  CUDA: call it before launching anything. */
bool mesh_inproc_load(const mesh_dev *d, const char *dir, mesh_image2 *out, char **err);
void mesh_image2_free(const mesh_dev *d, mesh_image2 *im);

/* A program in a slot of a mailbox, with the host side of its ring. */
typedef struct {
    mesh_mb2 mb;
    uint32_t slot;
    mesh_stream *stream;
    mesh_image2 image;
    uint32_t nodes;
    uint32_t tail; /* next ticket to publish */
} mesh_prog;

/* Writes the slot's table entry and manifest extension under the seqlock
 * (state LOADED), zeroes its control block, node records, trace and view.  Takes image. */
bool mesh_prog_install(const mesh_dev *d, mesh_mb2 mb, uint32_t slot, mesh_image2 *image, uint32_t view_off, uint32_t reads_mask,
                       mesh_prog *out, char **err);
void mesh_prog_free(const mesh_dev *d, mesh_prog *p);
size_t mesh_prog_ctl_off(const mesh_prog *p, size_t off);
size_t mesh_prog_view(const mesh_prog *p);
uint32_t mesh_prog_head(const mesh_prog *p);
uint32_t mesh_prog_counter(const mesh_prog *p, uint32_t i);
/* Rewinds the head to the tail, clears the shutdown, launches, waits (2 s) for every node online: *ms the time. */
bool mesh_prog_launch(const mesh_dev *d, mesh_prog *p, double *ms, char **err);
/* P_SHUTDOWN, then waits (3 s) until every node halted and the stream is done: *ms the time. */
bool mesh_prog_stop(const mesh_dev *d, mesh_prog *p, double *ms, char **err);
/* Publishes one task (waiting for the ticket that last used its ring entry): *ticket. */
bool mesh_prog_publish(mesh_prog *p, uint32_t op, uint32_t arg, uint32_t *ticket, char **err);
bool mesh_prog_done(const mesh_prog *p, uint32_t t);
/* Ticket t's result (5 s). */
bool mesh_prog_wait(const mesh_prog *p, uint32_t t, uint32_t *result, char **err);
bool mesh_prog_run(mesh_prog *p, uint32_t op, uint32_t arg, uint32_t *result, char **err);
/* One publish of op with args 0..n (consecutive tickets), then every result into results[n]. */
bool mesh_prog_batch(mesh_prog *p, uint32_t op, uint32_t n, uint32_t *results, char **err);
/* Appends bytes to the input queue (manifest `input`): data, then IQ_HEAD (release). */
bool mesh_prog_input_push(const mesh_prog *p, const uint8_t *b, size_t n, char **err);
/* One request through the window: the response bytes (*resp malloc'd, *rn). */
bool mesh_prog_request(mesh_prog *p, const uint8_t *req, size_t n, uint8_t **resp, size_t *rn, char **err);

/* First-fit-free sequential view placement: whole pages from VIEW_HEAP. */
typedef struct {
    uint32_t next;
} mesh_heap;
mesh_heap mesh_heap_new(void);
bool mesh_heap_alloc(mesh_heap *h, uint32_t bytes, uint32_t *at, char **err);

/* The manifest's `reads` as a slot mask over (name, slot) pairs; a name not loaded is an error. */
typedef struct {
    const char *name;
    uint32_t slot;
} mesh_slot_name;
bool mesh_reads_mask(const mesh_manifest *m, const mesh_slot_name *slots, size_t n, uint32_t *mask, char **err);

#endif
