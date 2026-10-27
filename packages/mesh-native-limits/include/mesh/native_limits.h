/* @ajax/mesh-native-limits: a native worker's limits (manifest cpu, mem_mb,
 * nice, background; mesh-host native.rs Limits).  On Linux cpu and mem_mb run
 * the process in `systemd-run --user --scope` (CPUQuota, MemoryMax and
 * MemorySwapMax = 0), and its cgroup is read back after the spawn: a limit
 * that is not in force refuses the start.  macOS cannot cap CPU or memory:
 * those are refused there; nice and background work on both. */
#ifndef MESH_NATIVE_LIMITS_H
#define MESH_NATIVE_LIMITS_H

#include "mesh/manifest.h"

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    uint32_t cpu_pct; /* CPUQuota, percent of one core (200 = 2 cores); 0 none */
    uint64_t mem;     /* MemoryMax bytes (and no swap); 0 none */
    uint32_t nice;
    bool background;
} mesh_native_limits;

mesh_native_limits mesh_native_limits_of(const mesh_manifest *m);
bool mesh_native_limits_scoped(const mesh_native_limits *l);
bool mesh_native_limits_eq(const mesh_native_limits *a, const mesh_native_limits *b);
/* LIMIT_* bits. */
uint32_t mesh_native_limits_bits(const mesh_native_limits *l);
/* Whether this machine can enforce l (a refusal says why).  Linux: a
 * transient user scope with those properties must start; probed once and cached. */
bool mesh_native_limits_available(const mesh_native_limits *l, char **err);
/* A process's cgroup v2 limits as l wants them (Linux; read after its exec). */
bool mesh_native_check_cgroup(pid_t pid, const mesh_native_limits *l, char **err);
/* Rust's {:?} of the limits: "Limits { cpu_pct: 200, mem: 1073741824, nice: 5, background: true }" (malloc'd). */
char *mesh_native_limits_debug(const mesh_native_limits *l);

#endif
