/* @ajax/mesh-exec-wasm-imports: the `mesh::*` imports (mesh-abi v2::wasm) of meshd's wasm
 * executor, ported from mesh-host/src/wasm.rs (`LaneData`, `linker`, `copy_out`, `copy_in`).
 *
 * A guest has no pointer into the mailbox: every access names a window (W_VIEW, W_EXCHANGE for
 * the lockstep program, W_PEER + s for a view `peer` resolved for this task) and an offset, and is
 * bounds-checked against the slot's own words exactly as mesh-rt's View is (out of range: reads 0,
 * writes dropped, P_FAULT = FAULT_VIEW_BOUNDS | off << 8, the first fault kept; meshd fails the
 * slot). Loads are acquire, stores release, read-modify-writes acq_rel. */
#ifndef MESH_EXEC_WASM_IMPORTS_H
#define MESH_EXEC_WASM_IMPORTS_H

#include "mesh/abi/base.h"
#include "mesh/mailbox.h"
#include "wasm/interp.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A lane's store data: its slot's windows, the task's peer views, the budget. */
typedef struct {
    mesh_mailbox mb;
    uint32_t slot, node, nodes;
    size_t view_off; /* the own view: mailbox offset, bytes */
    uint32_t view_len;
    uint32_t req_off, req_max;
    uint32_t resp_off, resp_max;
    uint32_t input_off, input_len;
    bool lockstep;
    /* Peer views resolved for the running task (`mesh::peer`). */
    bool peer_set[MESH_PROG_SLOTS];
    size_t peer_off[MESH_PROG_SLOTS];
    uint32_t peer_len[MESH_PROG_SLOTS];
    uint32_t ticket;
    /* The task's epoch budget (re-armed after an exchange's wait). */
    uint64_t ticks;
    /* The engine's epoch counter (the deadline counts its ticks). */
    const _Atomic uint64_t *epoch;
} mesh_wasm_lane_data;

/* Reads the slot's table entry (stable while it runs: meshd rewrites it only halted). */
void mesh_wasm_lane_data_init(mesh_wasm_lane_data *d, mesh_mailbox mb, uint32_t slot, uint32_t node, uint32_t nodes,
                              uint32_t task_ms, const _Atomic uint64_t *epoch);
/* A new task: no peer views resolved, its ticket. */
void mesh_wasm_lane_data_task(mesh_wasm_lane_data *d, uint32_t ticket);

/* The slot's first fault (P_FAULT keeps only the first). */
void mesh_wasm_lane_fault(const mesh_wasm_lane_data *d, uint32_t code);
/* The mailbox offset of n bytes at off in window h, align-aligned: true with *at, or false (and
 * the fault mesh-rt's View records). */
bool mesh_wasm_lane_at(const mesh_wasm_lane_data *d, uint32_t h, uint32_t off, uint32_t n, size_t align, size_t *at);
/* The slot's reads mask now: its own entry under the PT_GEN seqlock. */
uint32_t mesh_wasm_lane_reads_mask(const mesh_wasm_lane_data *d);
/* Slot s's view for this task, if `reads` lists it: its length, or -1. */
int32_t mesh_wasm_lane_peer(mesh_wasm_lane_data *d, uint32_t s);

/* Window bytes at mailbox offset a into dst (word loads where aligned), and back. */
void mesh_wasm_copy_out(mesh_mailbox mb, size_t a, uint8_t *dst, size_t n);
void mesh_wasm_copy_in(mesh_mailbox mb, size_t a, const uint8_t *src, size_t n);

/* The `mesh::*` imports, shared by every image: an instance's user pointer must be its lane's
 * mesh_wasm_lane_data. */
const wasm_host_func *mesh_wasm_imports(size_t *n);

#endif
