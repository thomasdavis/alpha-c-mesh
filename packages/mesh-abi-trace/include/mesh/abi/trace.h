/* @ajax/mesh-abi-trace: Each program's ring of task trace records.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_TRACE_H
#define MESH_ABI_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- trace */
/* Program s's last TRACE_PER_PROG task records, record n = atom.add(P_TRACE_N)
 * at `trace_rec(s, n)`: { slot | node << 8 | op << 16, ticket, start_lo, end_lo }
 * (globaltimer low words). */
#define MESH_TRACE_BASE 0x1A000
#define MESH_TRACE_PER_PROG 128u
#define MESH_TRACE_BYTES 16

/* Program s's last TRACE_PER_PROG task records, record n = atom.add(P_TRACE_N)
 * at `trace_rec(s, n)`: { slot | node << 8 | op << 16, ticket, start_lo, end_lo }
 * (globaltimer low words). */
static inline size_t mesh_trace_rec(uint32_t slot, uint32_t n) {
    return MESH_TRACE_BASE + MESH_TRACE_BYTES * ((size_t)MESH_TRACE_PER_PROG * (size_t)slot + (size_t)(n % MESH_TRACE_PER_PROG));
}

#endif
