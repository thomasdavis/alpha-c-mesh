/* @ajax/mesh-args: meshd's command line (args.rs; hand-rolled: a dozen flags
 * do not need a parser). Errors are the Rust's, word for word; the CUDA-only
 * flags are refused under another backend (no silent ignoring). */
#ifndef MESH_ARGS_H
#define MESH_ARGS_H

#include "mesh/backend.h"
#include "mesh/cluster_conf.h"
#include "mesh/strmap.h"

#include <stdbool.h>
#include <stdint.h>

#define MESH_ARGS_USAGE                                                                                                          \
    "usage: meshd [--backend cuda|metal|wasm (default: metal on macOS, else cuda; wasm: CPU programs only)] [--programs out/programs] " \
    "[--autoload] [--state FILE] [--wasm-cache DIR] [--sms N] [--control 127.0.0.1:7700] [--http 0.0.0.0:8088] [--cluster FILE "       \
    "--system K | --system 0 --peer HOST:7701 | --system 1 --peer-listen ADDR:7701] [--fleet-state FILE (a fleet member's term, vote " \
    "and desired state; required with --cluster/--peer)] [--jobs-state FILE (the job table's log; default: the fleet state's "          \
    "FILE.jobs)] [--blobs DIR (job items' payloads; default: the fleet state's FILE.blobs)] [--blobs-cap MB (default 4096)] "          \
    "[--nvjitlink /usr/local/cuda/lib64] [--cuda-lib DIR (or $MESH_CUDA_LIB)] [--isolation blink|mps [--mps-dir DIR] [--mps-pool 8]] " \
    "[--logs DIR (native workers' output; default logs)] [--models DIR (native workers' model files: MESH_MODELS)]  (--sms, "          \
    "--nvjitlink, --cuda-lib, --isolation, --mps-*: CUDA only)"

/* The platform's backend when --backend is not given: Metal on macOS, CUDA elsewhere. */
#ifdef __APPLE__
#define MESH_DEFAULT_BACKEND MESH_BACKEND_METAL
#else
#define MESH_DEFAULT_BACKEND MESH_BACKEND_CUDA
#endif

typedef struct {
    uint32_t backend;
    char *wasm_cache; /* NULL: none */
    bool has_sms;
    uint32_t sms;
    char *control, *http;
    bool has_system;
    uint32_t system;
    bool cluster; /* false: a single machine */
    mcc_topology topo;
    char *nvjitlink, *programs, *state; /* state: NULL none */
    bool autoload;
    mesh_isolation_mode isolation;
    char *mps_dir;
    bool has_mps_pool;
    uint32_t mps_pool;
    char *logs, *fleet_state, *jobs_state, *blobs, *models; /* NULL: none */
    uint64_t blobs_cap_mb;
    bool has_keys; /* --cluster FILE: its keys (the inference services' addresses) */
    ms_map keys;
} mesh_args;

/* --backend alone (main needs it first): 0 and *kind, or -1 and *err. */
int mesh_args_backend_of(int argc, char **argv, uint32_t *kind, char **err);
/* The command line (argv without the program name): 0, or -1 and *err (USAGE for --help). */
int mesh_args_parse(int argc, char **argv, mesh_args *a, char **err);
void mesh_args_free(mesh_args *a);

#endif
