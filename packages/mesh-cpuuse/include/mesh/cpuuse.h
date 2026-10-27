/* @ajax/mesh-cpuuse: what the CPU executors cost, per slot (mesh-host
 * cpuuse.rs): CPU time and resident bytes of a slot's wasm lanes (each lane
 * adds its own thread's CPU time and its linear memory) or native worker
 * processes (the driver stores their sampled counters).  The load sampler
 * turns them into PL_HOST fields and the machine's S_EXEC_* totals.
 * Counters are cumulative and never reset. */
#ifndef MESH_CPUUSE_H
#define MESH_CPUUSE_H

#include "mesh/abi/program_table.h"

#include <stdatomic.h>
#include <stdint.h>

typedef struct {
    _Atomic uint64_t cpu_ns; /* CPU ns, summed over the slot's lanes or processes, monotonic */
    _Atomic uint64_t rss;    /* resident bytes now */
    _Atomic uint32_t units;  /* lanes running (wasm) / live worker processes (native) */
} mesh_slot_use;

extern mesh_slot_use mesh_cpuuse_slots[MESH_PROG_SLOTS];
/* Native workers of this meshd, machine-wide: live processes, spawns, crashes. */
extern _Atomic uint32_t mesh_cpuuse_workers, mesh_cpuuse_worker_spawns, mesh_cpuuse_worker_crashes;

void mesh_slot_use_add_cpu(mesh_slot_use *u, uint64_t ns);
/* Adds d (which may be negative) to rss, never below 0. */
void mesh_slot_use_add_rss(mesh_slot_use *u, int64_t d);
void mesh_slot_use_get(mesh_slot_use *u, uint64_t *cpu_ns, uint64_t *rss, uint32_t *units);

/* The calling thread's CPU time, ns (CLOCK_THREAD_CPUTIME_ID). */
uint64_t mesh_thread_cpu_ns(void);

/* A lane's meter: adds its thread's CPU time since the last tick (and its
 * memory's growth) to its slot.  Ended: its memory leaves the slot's RSS. */
typedef struct {
    uint32_t slot;
    uint64_t cpu, rss;
} mesh_meter;

void mesh_meter_start(mesh_meter *m, uint32_t slot); /* on the lane's own thread */
void mesh_meter_tick(mesh_meter *m, uint64_t rss);   /* rss: the lane's bytes now */
void mesh_meter_end(mesh_meter *m);                  /* Rust's Drop */

#endif
