/* @ajax/mesh-load-prog: one slot's load sampler (load.rs Prog).
 *
 * Each sample (about once a second) reads the program's node load records
 * (NL_*: the GPU share its nodes obtained), node records (busy time, tasks),
 * the trace records written since the last sample (task latencies) and P_DONE,
 * and for a CPU executor (wasm, native) its CPU time and RSS (mesh-cpuuse),
 * and writes the program's PLOAD record: the last second and a window of
 * LOAD_WINDOW samples. Another image in the slot (PT_PTX_HASH, PT_LOADS)
 * starts a fresh baseline. */
#ifndef MESH_LOAD_PROG_H
#define MESH_LOAD_PROG_H

#include "mesh/abi.h"
#include "mesh/load_share.h"
#include "mesh/mailbox.h"

#include <stdbool.h>
#include <stdint.h>

/* One node load record (NL_*) now, its longest gap and its window number. */
mesh_nl mesh_nl_read(mesh_mailbox mb, uint32_t s, uint32_t n, uint32_t *max_gap_ns, uint32_t *win);

typedef struct {
    mesh_nl d;
    uint32_t *lat; /* task durations (ns) */
    size_t nlat;
    uint64_t busy_ns, busy_tasks, tasks, cpu_d;
} mesh_prog_window;

typedef struct {
    uint64_t hash;
    uint32_t loads; /* (PT_PTX_HASH, PT_LOADS): another image resets everything */
    uint32_t nodes;
    mesh_nl nl[MESH_PROG_MAX_NODES];
    uint64_t busy_ns[MESH_PROG_MAX_NODES];
    uint32_t busy_tasks[MESH_PROG_MAX_NODES];
    uint32_t done, trace_n;
    mesh_prog_window win[MESH_LOAD_WINDOW];
    size_t nwin;
    bool has_task_us;
    float task_us;
    uint64_t cpu_base, cpu_last;
    bool started; /* a baseline exists */
} mesh_prog_sampler;

void mesh_prog_sampler_reset(mesh_prog_sampler *p);
/* One sample of slot s: its record and its GPU deltas (for the machine's sums) and the window's longest gap. */
void mesh_prog_sample(mesh_prog_sampler *p, mesh_mailbox mb, uint32_t s, double dt, uint32_t win, uint8_t out[MESH_PLOAD_BYTES], mesh_nl *d,
                      uint32_t *max_gap);

#endif
