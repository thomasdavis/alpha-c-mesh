/* @ajax/mesh-abi-host-nics: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/host_nics.h"

const char *const MESH_NET_VIRTUAL[16] = {"lo", "docker", "br-", "bridge", "virbr", "lxdbr", "veth", "cni", "flannel", "vmenet", "tailscale", "utun", "tun", "wg", "gif", "stf"};

const mesh_field MESH_NIC_FIELDS[11] = {
    {MESH_NI_NAME, 16},
    {MESH_NI_RX_BYTES, 8},
    {MESH_NI_TX_BYTES, 8},
    {MESH_NI_RX_PACKETS, 8},
    {MESH_NI_TX_PACKETS, 8},
    {MESH_NI_RX_ERRS, 8},
    {MESH_NI_TX_ERRS, 8},
    {MESH_NI_RX_DROP, 8},
    {MESH_NI_TX_DROP, 8},
    {MESH_NI_LINKS, 4},
    {MESH_NI_COUNT, 4},
};

bool mesh_nic_virtual(const uint8_t *name, size_t len) {
    for (size_t i = 0; i < MESH_NET_VIRTUAL_LEN; i++) {
        const char *p = MESH_NET_VIRTUAL[i];
        size_t n = 0;
        while (p[n]) n++;
        if (len < n) continue;
        size_t j = 0;
        while (j < n && name[j] == (uint8_t)p[j]) j++;
        if (j == n) return true;
    }
    return false;
}

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_S_NICS % 8 == 0), "S_NICS.is_multiple_of(8)");
_Static_assert((MESH_NIC_BYTES % 8 == 0), "NIC_BYTES.is_multiple_of(8)");
