/* @ajax/mesh-abi-host-nics: NIC records (NI_*) and the virtual interfaces left out of the total.
 *
 * Part of alpha-c-mesh, a C port of alpha-rust-mesh (README.md says from where). */
#ifndef MESH_ABI_HOST_NICS_H
#define MESH_ABI_HOST_NICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "mesh/abi/fields.h"

/* NIC records (S_NICS, S_NET_TOTAL). */
#define MESH_S_NICS 0xb00
#define MESH_NIC_BYTES 0x60
#define MESH_MAX_NICS 8
#define MESH_NI_NAME 0x00 /* 16 bytes NUL padded */
#define MESH_NI_RX_BYTES 0x10 /* u64 each, cumulative (the OS's counters, extended past wraps and resets) */
#define MESH_NI_TX_BYTES 0x18
#define MESH_NI_RX_PACKETS 0x20
#define MESH_NI_TX_PACKETS 0x28
#define MESH_NI_RX_ERRS 0x30
#define MESH_NI_TX_ERRS 0x38
#define MESH_NI_RX_DROP 0x40
#define MESH_NI_TX_DROP 0x48
#define MESH_NI_LINKS 0x50 /* u32 bit k: the link to system k is on it */
#define MESH_NI_COUNT 0x54 /* u32 interfaces summed (1; the total: how many) */
/* Interface name prefixes left out of the total: loopback, container and VM
 * bridges and veths, overlays and tunnels. */
#define MESH_NET_VIRTUAL_LEN 16
extern const char *const MESH_NET_VIRTUAL[16];
#define MESH_NIC_FIELDS_LEN 11
extern const mesh_field MESH_NIC_FIELDS[11];

/* NIC record i. */
static inline size_t mesh_nic(size_t i) { return MESH_S_NICS + MESH_NIC_BYTES * i; }
/* Whether an interface name (len bytes, not NUL terminated) starts with one of NET_VIRTUAL's prefixes. */
bool mesh_nic_virtual(const uint8_t *name, size_t len);

#endif
