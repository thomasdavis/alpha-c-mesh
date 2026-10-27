/* @ajax/mesh-nic: the machine's network interfaces' counters (net.rs, "Network I/O").
 *
 * Read once a second: Linux /proc/net/dev, macOS the routing sysctl's 64-bit
 * if_data64. Each OS counter is extended to a monotonic u64 (a narrower one
 * that went down wrapped; a 64-bit one that went down was reset), and a total
 * sums every physical interface (mesh-abi NET_VIRTUAL prefixes left out) that
 * never goes down. NIC records (NI_*) are what the host block publishes. */
#ifndef MESH_NIC_H
#define MESH_NIC_H

#include "mesh/abi/host_nics.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_NIC_NAME_MAX 64

/* The NIC counters in NI_* order: rx/tx bytes, packets, errors, drops. */
extern const size_t MESH_NIC_COUNTERS[8];

/* One interface's raw counters as the OS reports them, and each counter's width in bits. */
typedef struct {
    char name[MESH_NIC_NAME_MAX];
    uint64_t v[8];
    uint32_t bits[8];
} mesh_nic_raw;

/* A raw counter extended to a monotonic u64. */
typedef struct {
    uint64_t last, value;
} mesh_nic_ext;

mesh_nic_ext mesh_nic_ext_new(uint64_t raw);
/* Advances by the delta to raw; returns it. */
uint64_t mesh_nic_ext_update(mesh_nic_ext *e, uint64_t raw, uint32_t bits);

/* `/proc/net/dev`'s text: 0 and *out (free it), or -1 and *err (free it). */
int mesh_nic_parse_proc_net_dev(const char *text, mesh_nic_raw **out, size_t *n, char **err);
/* This machine's interfaces now. */
int mesh_nic_read(mesh_nic_raw **out, size_t *n, char **err);
/* The interface holding local address `ip` (IPv4 or IPv6 text): 0 and name, or -1 and *err. */
int mesh_nic_iface_of(const char *ip, char name[MESH_NIC_NAME_MAX], char **err);

/* An interface's extended counters now (one sample's answer). */
typedef struct {
    char name[MESH_NIC_NAME_MAX];
    uint64_t v[8];
} mesh_nic_now;

typedef struct {
    char name[MESH_NIC_NAME_MAX];
    mesh_nic_ext e[8];
} mesh_nic_entry_;

/* The sampler's state: each interface's extended counters, and the total. */
typedef struct {
    mesh_nic_entry_ *ext;
    size_t n;
    uint64_t total[8];
    uint32_t counted;
} mesh_nics;

/* One sample: updates every interface; *now (free it) lists each by name, in raw's order. */
void mesh_nics_sample(mesh_nics *s, const mesh_nic_raw *raw, size_t n, mesh_nic_now **now, size_t *nnow);
const uint64_t *mesh_nic_lookup(const mesh_nic_now *now, size_t n, const char *name);
void mesh_nics_free(mesh_nics *s);

/* A NIC record's bytes (MESH_NIC_BYTES): the name (at most 15 bytes), the counters, links, count. */
void mesh_nic_record(uint8_t out[MESH_NIC_BYTES], const char *name, const uint64_t v[8], uint32_t links, uint32_t count);

#endif
