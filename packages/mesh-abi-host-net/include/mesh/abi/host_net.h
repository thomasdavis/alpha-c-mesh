/* @ajax/mesh-abi-host-net: Network I/O counters: the NIC sample clock, the control port's and HTTP relay's bytes.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_NET_H
#define MESH_ABI_HOST_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Network I/O (0x640..): cumulative u64 counters since meshd started; viewers */
/* derive rates from two snapshots.  Link and port counters are live (read at */
/* `snap`, timed by S_UPTIME_MS); NIC counters are sampled about once a second */
/* (timed by S_NET_AT_MS). */
/* u64 uptime ms of the last NIC sample (0: none yet). */
#define MESH_S_NET_AT_MS 0x640
/* u32 NIC records at S_NICS (<= MAX_NICS): the interfaces that carry (or
 * carried) a peer link, by name. */
#define MESH_S_NET_NICS 0x648
/* u32 NIC samples taken. */
#define MESH_S_NET_SAMPLES 0x64c
/* The control port: u64 request bytes read, reply bytes written, requests. */
#define MESH_S_CTL_RX 0x650
#define MESH_S_CTL_TX 0x658
#define MESH_S_CTL_REQS 0x660
/* The HTTP relay (--http): u64 request-head bytes read, response bytes written, connections. */
#define MESH_S_HTTP_RX 0x668
#define MESH_S_HTTP_TX 0x670
#define MESH_S_HTTP_CONNS 0x678
/* A NIC record (NI_*) summing every interface except loopback and the
 * virtual ones whose traffic also crosses a physical one or never leaves the
 * host (NET_VIRTUAL prefixes); NI_COUNT says how many it sums. */
#define MESH_S_NET_TOTAL 0x680

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_S_NET_AT_MS % 8 == 0), "S_NET_AT_MS.is_multiple_of(8)");
_Static_assert((MESH_S_CTL_RX % 8 == 0), "S_CTL_RX.is_multiple_of(8)");
_Static_assert(MESH_S_HTTP_CONNS + 8 <= MESH_S_NET_TOTAL, "S_HTTP_CONNS + 8 <= S_NET_TOTAL");
_Static_assert((MESH_S_NET_TOTAL % 8 == 0), "S_NET_TOTAL.is_multiple_of(8)");

#endif
