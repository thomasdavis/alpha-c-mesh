/* @ajax/mesh-load-host: this machine's side of the load telemetry (load.rs):
 *
 * - `mesh_host_read`: one sample of the host (per-CPU times, load average,
 *   PSI, memory, swap, meshd's own CPU, RSS and threads, free disk for the
 *   catalog, the hottest thermal zone), from Linux's /proc and /sys;
 * - `mesh_host_derive`: rates between two samples (per-CPU utilisation by core
 *   class, iowait, PSI shares, meshd's CPU, swap-outs);
 * - `mesh_machine_bytes`: the host block's machine region (S_LOAD..S_MLOAD);
 * - `mesh_exec_totals`: the CPU executors' sums into it;
 * - `mesh_loadrep`: the member summary sent to the cluster as F_LOADREP. */
#ifndef MESH_LOAD_HOST_H
#define MESH_LOAD_HOST_H

#include "mesh/abi/host_load.h"
#include "mesh/abi/host_mload.h"
#include "mesh/abi/host_pload.h"
#include "mesh/load_proc.h"
#include "mesh/load_share.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What one host sample read. */
typedef struct {
    mesh_cpu_times cpu_all;
    mesh_cpu_times cpus[MESH_MAX_CPUS];
    bool have[MESH_MAX_CPUS]; /* per CPU id: online (a line in /proc/stat) */
    size_t ncpus;             /* the highest id + 1 */
    float loadavg[3];
    bool has_psi;
    mesh_psi psi[3]; /* cpu, memory, io */
    uint32_t mem_total_mb, mem_avail_mb, swap_total_mb, swap_used_mb, compressed_mb;
    bool has_mem_level;
    uint32_t mem_level;
    float mem_free;
    uint64_t swap_out;
    uint64_t self_cpu_us;
    float self_rss_mb;
    uint32_t self_threads;
    uint32_t disk_free_mb, disk_total_mb;
    bool has_thermal;
    uint32_t thermal;
    bool has_temp;
    float temp_c;
    bool has_gpu_util;
    float gpu_util;
} mesh_host_sample;

/* Per-CPU core classes (MESH_CORE_*) for n CPUs, and what they came from
 * ("cpu_capacity", "cpuinfo_max_freq" or "none"). */
const char *mesh_core_classes(uint8_t *out, size_t n);
bool mesh_has_psi(void);
/* One sample; dir: the file system whose free space counts (the catalog). 0, or -1 and *err. */
int mesh_host_read(mesh_host_sample *h, const char *dir, bool psi, char **err);

/* Rates between two host samples. */
typedef struct {
    float util[MESH_MAX_CPUS];
    bool have_util[MESH_MAX_CPUS];
    size_t n;
    float all, big, little, iowait;
    bool has_psi;
    float psi[5]; /* some cpu, some mem, full mem, some io, full io */
    float meshd_cpu, swap_out_s;
} mesh_host_derived;

void mesh_host_derive(const mesh_host_sample *now, const mesh_host_sample *prev, const uint8_t *cls, size_t ncls, double dt,
                      mesh_host_derived *d);

#define MESH_MACHINE_BYTES (MESH_S_MLOAD - MESH_S_LOAD)

/* The machine region (S_LOAD .. S_MLOAD): MESH_MACHINE_BYTES into out. */
void mesh_machine_bytes(uint8_t out[MESH_MACHINE_BYTES], const mesh_host_sample *h, const mesh_host_derived *d, const uint8_t *cls,
                        size_t ncls, const mesh_share *gpu1, const mesh_share *gpu10, uint32_t max_gap_ns, float gaps_s, float obs,
                        uint64_t at_ms, uint32_t samples, uint32_t sample_us);

/* The CPU executors' totals into the machine region: the PLF_HOST program
 * records' CPU and RSS, wasm lanes, and the native workers' and log counts. */
void mesh_exec_totals(uint8_t m[MESH_MACHINE_BYTES], const uint8_t (*recs)[MESH_PLOAD_BYTES], const uint32_t *execs, size_t n,
                      uint32_t workers, uint32_t spawns, uint32_t crashes, uint64_t log_dropped);

/* This machine's member summary (mload(k) from ML_SHARE on: F_LOADREP's body). */
void mesh_loadrep(uint8_t out[MESH_LOADREP_BYTES], const uint8_t m[MESH_MACHINE_BYTES]);

#endif
