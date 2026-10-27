/* @ajax/mesh-sched-member: one fleet member as the scheduler sees it -- F_JOIN's capabilities,
 * F_PERF's (CPUs, RAM, the view heap, link RTTs, failures) and F_LOADREP's live load -- and a
 * program's measured performance on a member (F_PERF), keyed by (program, member). */
#ifndef MESH_SCHED_MEMBER_H
#define MESH_SCHED_MEMBER_H

#include "mesh/abi/base.h"
#include "mesh/strmap.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Rust's Option<f32>. */
typedef struct {
    bool some;
    float v;
} mof32;

static inline mof32 mof32_some(float v) { return (mof32){true, v}; }
static inline mof32 mof32_none(void) { return (mof32){false, 0}; }
static inline bool mof32_eq(mof32 a, mof32 b) { return a.some == b.some && (!a.some || a.v == b.v); }

typedef struct {
    uint32_t id;
    bool up;
    uint64_t down_ms;  /* how long it has been down (0 while up) */
    bool cordoned;
    char host[64];
    char gpu[32];      /* its GPU executor (cuda, metal; wasm for a CPU-only member) */
    char arch[32];
    uint32_t cc, sms, blocks_per_sm;
    /* Its GPU's limits for one node (F_JOIN): threads and shared bytes per block; 0: not reported. */
    uint32_t max_threads, max_shared;
    uint32_t cpus, cores_big, cores_little, ram_mb, mem_avail_mb, disk_free_mb;
    mof32 gpu_share;   /* meshd's GPU share over 10 s */
    float cpu_util, cpu_big, cpu_little; /* host CPU utilisation 0..1, by class */
    mof32 psi_cpu, psi_mem;
    float meshd_cpu;
    mof32 rtt_us[MESH_MAX_SYSTEMS]; /* link RTT to each member in µs */
    size_t nrtt;                    /* Rust's Vec length (a k past it is None) */
    char *busy;         /* the owner policy holds it busy (why); NULL: not busy */
    bool load_known;    /* its load report arrived */
    uint32_t heap_kb, slots_free; /* the view heap's largest free run (KiB) and free slots */
    ms_map fails;       /* programs that failed here: name -> why */
} msm_member;

/* Member::default (everything zero, strings empty). */
msm_member msm_member_default(void);
msm_member msm_member_clone(const msm_member *m);
void msm_member_free(msm_member *m);
void msm_set_str(char *dst, size_t cap, const char *s);
#define MSM_SET(field, s) msm_set_str((field), sizeof(field), (s))

/* Its executors: the GPU's, then wasm and native (every meshd has them). Returns the count (<= 3). */
size_t msm_executors(const msm_member *m, const char *out[3]);
/* The executor a program listing backends[0..n) runs on here (the first one listed it has), or NULL. */
const char *msm_exec_for(const msm_member *m, char *const *backends, size_t n);
mof32 msm_rtt_to(const msm_member *m, uint32_t k); /* 0 to itself */

/* A program's measured performance on one member (F_PERF). */
typedef struct {
    uint64_t hash;
    char exec[16];
    bool run;
    mof32 task_us; /* mean node busy time per task (µs), else the trace p50 */
    float tasks_s;
    float cpu;     /* CPU cores it uses (wasm and native) */
    mof32 share;
    float rss_mb;
} msm_perf;

bool msm_perf_eq(const msm_perf *a, const msm_perf *b);

/* BTreeMap<(String, u32), Perf>: sorted by (program name, member). */
typedef struct {
    char **name;
    uint32_t *member;
    msm_perf *perf;
    size_t n, cap;
} msm_perfs;

const msm_perf *msm_perfs_get(const msm_perfs *p, const char *name, uint32_t member);
void msm_perfs_set(msm_perfs *p, const char *name, uint32_t member, const msm_perf *perf);
/* Index range [*lo, *hi) of the entries of program name (Rust's range((name, 0)..=(name, MAX))). */
void msm_perfs_range(const msm_perfs *p, const char *name, size_t *lo, size_t *hi);
void msm_perfs_remove_at(msm_perfs *p, size_t i);
msm_perfs msm_perfs_clone(const msm_perfs *p);
void msm_perfs_free(msm_perfs *p);

#endif
