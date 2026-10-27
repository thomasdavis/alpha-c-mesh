/* @ajax/mesh-net-counters: meshd's live network counters (mesh-abi "Network I/O").
 *
 * Per peer link, the bytes and frames by kind meshd reads and writes; the
 * control port's and the HTTP relay's bytes and requests. Every counter is a
 * cumulative u64 since meshd started, a relaxed atomic bumped once per frame
 * or request by the thread that moved it; `fill` copies them into a host
 * status block at snapshot time (viewers derive rates). */
#ifndef MESH_NET_COUNTERS_H
#define MESH_NET_COUNTERS_H

#include "mesh/abi/base.h"
#include "mesh/abi/host_links.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    _Atomic uint64_t rx_bytes, tx_bytes;
    _Atomic uint64_t rx[MESH_NET_KINDS], tx[MESH_NET_KINDS];
} mesh_link_counters;

typedef struct {
    mesh_link_counters links[MESH_MAX_SYSTEMS];
    _Atomic uint64_t ctl[3];  /* bytes read, bytes written, requests */
    _Atomic uint64_t http[3]; /* head bytes read, response bytes written, connections */
} mesh_net_counters;

void mesh_net_counters_init(mesh_net_counters *c);
/* A frame of type `ty`, `bytes` long (type byte included), read from system k. */
void mesh_net_counters_rx(mesh_net_counters *c, uint32_t k, uint8_t ty, size_t bytes);
/* A frame written to system k (`f` starts with its type byte). */
void mesh_net_counters_tx(mesh_net_counters *c, uint32_t k, const uint8_t *f, size_t len);
/* A control request: its line (and body) read, its reply written. */
void mesh_net_counters_ctl(mesh_net_counters *c, size_t rx, size_t tx);
/* An HTTP connection: its head read, its response written. */
void mesh_net_counters_http(mesh_net_counters *c, size_t rx, size_t tx);
/* Writes the live counters into a host status block (SNAP_HOST bytes). */
void mesh_net_counters_fill(mesh_net_counters *c, uint8_t *block);

#endif
