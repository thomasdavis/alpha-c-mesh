/* @ajax/mesh-manifest: `program.toml`, a program's manifest (mesh-host
 * manifest.rs; docs/plan-program-loading.md §3.3).  A hand-rolled
 * `key = value` format (mesh-toml): unknown and repeated keys are refused.
 * `abi` is parsed but checked by the loader (its refusal reason is `abi`).
 * Every refusal message is the Rust's, byte for byte. */
#ifndef MESH_MANIFEST_H
#define MESH_MANIFEST_H

#include "mesh/needs.h"
#include "mesh/text.h"

#include "mesh/abi/control_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name;
    uint32_t ty; /* FT_* */
    uint32_t off;
} mesh_manifest_field;

/* The request window, view offsets: requests at req_off (req_max bytes), responses at resp_off. */
typedef struct {
    uint32_t req_off, req_max, resp_off, resp_max;
} mesh_manifest_window;

/* A view region (offset, bytes). */
typedef struct {
    uint32_t off, len;
} mesh_manifest_region;

/* The manifest's placement declaration (mesh-abi "placement"): which view
 * regions meshd moves between the machines of a cluster. */
typedef struct {
    uint32_t cap; /* PLACE_CAP_PIN / PLACE_CAP_SPLIT */
    mesh_manifest_region *state; /* the shared state, each 4-aligned */
    size_t nstate;
    uint32_t frame; /* the u32 frame number's view offset (inside a state region) */
    mesh_manifest_region *out; /* part b (task arg b) is the b-th of fanout equal pieces of each */
    size_t nout;
    uint32_t show; /* the show region's view offset (SH_*) */
    uint32_t fanout;
} mesh_manifest_place;

size_t mesh_manifest_place_state_bytes(const mesh_manifest_place *p);
uint32_t mesh_manifest_place_out_bytes(const mesh_manifest_place *p);
/* Part b's (view offset, bytes) in each output region: out[0..nout). */
void mesh_manifest_place_part(const mesh_manifest_place *p, uint32_t b, mesh_manifest_region *out);
/* Part b's (offset, bytes) pieces within a show buffer (the regions concatenated). */
void mesh_manifest_place_part_in_buffer(const mesh_manifest_place *p, uint32_t b, mesh_manifest_region *out);
size_t mesh_manifest_place_part_bytes(const mesh_manifest_place *p);
/* Place::mask(lo..hi): bits lo..hi-1. */
uint32_t mesh_manifest_place_mask(uint32_t lo, uint32_t hi);

/* A consumer's inbox (mesh-abi pipe). */
typedef struct {
    uint32_t off, cap, rec;
    char *ty;
} mesh_manifest_inbox;

/* `feeds = ["consumer:type", ..]`. */
typedef struct {
    char *consumer;
    char *ty;
} mesh_manifest_feed;

typedef struct mesh_manifest {
    char *name;
    uint32_t abi;
    uint32_t view_bytes;
    uint32_t view_abi;
    uint32_t snap_bytes;
    uint32_t nodes;
    /* `nodes = "fit"`: decided at load on each machine (PT_NODES has the number). */
    bool nodes_fit;
    /* `warps` (1..=MAX_WARPS, default 1): a node is a block of 32 x warps threads on a GPU. */
    uint32_t warps;
    /* `shared_bytes`: the program's shared memory per node, after RT_SHARED_BYTES. */
    uint32_t shared_bytes;
    /* `scratch_bytes`: the program's own device memory on a GPU, zeroed at load. */
    uint32_t scratch_bytes;
    uint32_t sms;
    uint32_t policy; /* POLICY_* */
    uint32_t hz;
    uint32_t fanout;
    /* in_flight = "batch" (default): publish the next batch only once the previous one completed. */
    bool batch;
    uint32_t arg_mode; /* ARG_* */
    uint32_t arg_off;
    /* Program names, or ["*"] (http only). */
    char **reads;
    size_t nreads;
    /* The input queue (view offset, data bytes), 0/0 = none. */
    uint32_t input_off;
    uint32_t input_len;
    bool has_request;
    mesh_manifest_window request;
    uint32_t peer; /* PEER_MODE_* */
    char *counters[MESH_COUNTERS];
    mesh_manifest_field *fields;
    size_t nfields;
    bool autostart;
    /* `place`: NULL unless the program may be placed (pin / split). */
    mesh_manifest_place *place;
    /* `backends` (default ["cuda"]). */
    char **backends;
    size_t nbackends;
    /* `param`: a u32 the program reads (PX_PARAM); part of the image's identity. */
    uint32_t param;
    /* `float`: "ftz" (default) or "ieee" (wasm only). */
    bool ftz;
    /* `mem_mb`: wasm: each lane's linear-memory cap in MiB, default 64; native: MemoryMax, 0 = none. */
    uint32_t mem_mb;
    /* `task_ms` (wasm): a task's time budget, default 50. */
    uint32_t task_ms;
    /* Native workers only: `cpu` as a percent of one core, `mem_mb` in bytes, `nice`, `background`. */
    uint32_t cpu_pct;
    uint64_t mem;
    uint32_t nice;
    bool background;
    /* Native only: `device = "gpu"`. */
    bool device_gpu;
    /* `inbox = "off:cap:rec"`, `inbox_type`: NULL when none. */
    mesh_manifest_inbox *inbox;
    /* `feeds = ["consumer:type", ..]`. */
    mesh_manifest_feed *feeds;
    size_t nfeeds;
    /* The fleet scheduler's keys. */
    mesh_needs needs;
    /* `job_base = <off>` (policy "job"). */
    bool has_job_base;
    uint32_t job_base;
    /* `job_out = "off:rec"` (policy "job"). */
    bool has_job_out;
    uint32_t job_out_off, job_out_rec;
    /* `job_payload = "off:len"` (policy "job"). */
    bool has_job_payload;
    uint32_t job_payload_off, job_payload_len;
} mesh_manifest;

/* A job item's result record past its u32 (`job_out`), at most. */
#define MESH_JOB_OUT_MAX 256u

/* Parses manifest text. False with *err, e.g. "line 3: unknown key \"bogus\"". */
bool mesh_manifest_parse(const char *text, size_t n, mesh_manifest *out, char **err);
/* A program dir (or its program.toml): the manifest and its `<name>.ptx`
 * path (*ptx, malloc'd). Errors are "<path>: <why>". */
bool mesh_manifest_load(const char *path, mesh_manifest *out, char **ptx, char **err);
void mesh_manifest_free(mesh_manifest *m);
void mesh_manifest_copy(mesh_manifest *dst, const mesh_manifest *src);

/* Threads per node on a GPU (32 x warps). */
uint32_t mesh_manifest_threads(const mesh_manifest *m);
/* The dynamic shared memory a node's launch gets: the runtime's area and the program's. */
uint32_t mesh_manifest_smem(const mesh_manifest *m);
/* A native worker program (`backends = ["native"]`). */
bool mesh_manifest_native(const mesh_manifest *m);

/* Rust's derived `{:?}` of the Manifest (golden tests compare it with the Rust's). */
void mesh_manifest_debug(mesh_text *t, const mesh_manifest *m);

#endif
