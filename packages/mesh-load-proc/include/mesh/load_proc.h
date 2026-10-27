/* @ajax/mesh-load-proc: the pure parsers of load.rs: /proc/stat's CPU times,
 * the big/little core classes, PSI (the /proc/pressure files), /proc/meminfo,
 * /proc/loadavg and `key value` / `Key: value` files. Errors are the Rust's
 * text. The OS readers that feed them are mesh-load-host. */
#ifndef MESH_LOAD_PROC_H
#define MESH_LOAD_PROC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One CPU's (or all CPUs') time counters, in the OS's units. */
typedef struct {
    uint64_t busy, total, iowait;
} mesh_cpu_times;

/* Busy and iowait fractions between two readings (0, 0 when no time passed). */
void mesh_cpu_times_since(const mesh_cpu_times *now, const mesh_cpu_times *prev, float *busy, float *io);

/* `/proc/stat`'s cpu and cpuN lines: the aggregate, and per CPU id (offline CPUs:
 * have[i] false). per and have hold MESH_MAX_CPUS entries; *n is the highest id + 1. */
int mesh_parse_proc_stat(const char *text, mesh_cpu_times *all, mesh_cpu_times *per, bool *have, size_t *n, char **err);

/* Core classes (MESH_CORE_*) from per-CPU performance figures (cpu_capacity or
 * cpuinfo_max_freq; have[i] false: unknown): within 10 % of the fastest is big. */
void mesh_classify(const uint64_t *perf, const bool *have, size_t n, uint8_t *out);
/* The classes' busy fractions (all, big, little): means of the known per-CPU figures. */
void mesh_class_util(const float *util, const bool *have, const uint8_t *cls, size_t n, float *all, float *big, float *little);

/* A /proc/pressure file's `some` and `full` lines. */
typedef struct {
    float some_avg10;
    uint64_t some_total;
    float full_avg10;
    uint64_t full_total;
} mesh_psi;

int mesh_parse_psi(const char *text, mesh_psi *out, char **err);
/* The share of dt_us some task stalled, from two PSI totals (us). */
float mesh_psi_rate(uint64_t now, uint64_t prev, uint64_t dt_us);

typedef struct {
    uint64_t total_kb, avail_kb, swap_total_kb, swap_free_kb;
} mesh_meminfo;

int mesh_parse_meminfo(const char *text, mesh_meminfo *out, char **err);
int mesh_parse_loadavg(const char *text, float out[3], char **err);
/* A `key value` file's value (/proc/vmstat pswpout), or a `Key:  value` one's
 * (/proc/self/status VmRSS, Threads): true and *out, or false. */
bool mesh_proc_field(const char *text, const char *key, uint64_t *out);

#endif
