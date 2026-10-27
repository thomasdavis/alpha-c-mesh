/* Every constant of @ajax/mesh-abi-host-nics equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/host_nics.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_S_NICS, UINT64_C(2816));
    CHECK_U(MESH_NIC_BYTES, UINT64_C(96));
    CHECK_U(MESH_MAX_NICS, UINT64_C(8));
    CHECK_U(MESH_NI_NAME, UINT64_C(0));
    CHECK_U(MESH_NI_RX_BYTES, UINT64_C(16));
    CHECK_U(MESH_NI_TX_BYTES, UINT64_C(24));
    CHECK_U(MESH_NI_RX_PACKETS, UINT64_C(32));
    CHECK_U(MESH_NI_TX_PACKETS, UINT64_C(40));
    CHECK_U(MESH_NI_RX_ERRS, UINT64_C(48));
    CHECK_U(MESH_NI_TX_ERRS, UINT64_C(56));
    CHECK_U(MESH_NI_RX_DROP, UINT64_C(64));
    CHECK_U(MESH_NI_TX_DROP, UINT64_C(72));
    CHECK_U(MESH_NI_LINKS, UINT64_C(80));
    CHECK_U(MESH_NI_COUNT, UINT64_C(84));
    CHECK_U(MESH_NET_VIRTUAL_LEN, 16);
    CHECK_S(MESH_NET_VIRTUAL[0], "lo");
    CHECK_S(MESH_NET_VIRTUAL[1], "docker");
    CHECK_S(MESH_NET_VIRTUAL[2], "br-");
    CHECK_S(MESH_NET_VIRTUAL[3], "bridge");
    CHECK_S(MESH_NET_VIRTUAL[4], "virbr");
    CHECK_S(MESH_NET_VIRTUAL[5], "lxdbr");
    CHECK_S(MESH_NET_VIRTUAL[6], "veth");
    CHECK_S(MESH_NET_VIRTUAL[7], "cni");
    CHECK_S(MESH_NET_VIRTUAL[8], "flannel");
    CHECK_S(MESH_NET_VIRTUAL[9], "vmenet");
    CHECK_S(MESH_NET_VIRTUAL[10], "tailscale");
    CHECK_S(MESH_NET_VIRTUAL[11], "utun");
    CHECK_S(MESH_NET_VIRTUAL[12], "tun");
    CHECK_S(MESH_NET_VIRTUAL[13], "wg");
    CHECK_S(MESH_NET_VIRTUAL[14], "gif");
    CHECK_S(MESH_NET_VIRTUAL[15], "stf");
    CHECK_U(MESH_NIC_FIELDS_LEN, 11);
    CHECK_U(MESH_NIC_FIELDS[0].off, UINT64_C(0));
    CHECK_U(MESH_NIC_FIELDS[0].bytes, UINT64_C(16));
    CHECK_U(MESH_NIC_FIELDS[1].off, UINT64_C(16));
    CHECK_U(MESH_NIC_FIELDS[1].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[2].off, UINT64_C(24));
    CHECK_U(MESH_NIC_FIELDS[2].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[3].off, UINT64_C(32));
    CHECK_U(MESH_NIC_FIELDS[3].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[4].off, UINT64_C(40));
    CHECK_U(MESH_NIC_FIELDS[4].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[5].off, UINT64_C(48));
    CHECK_U(MESH_NIC_FIELDS[5].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[6].off, UINT64_C(56));
    CHECK_U(MESH_NIC_FIELDS[6].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[7].off, UINT64_C(64));
    CHECK_U(MESH_NIC_FIELDS[7].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[8].off, UINT64_C(72));
    CHECK_U(MESH_NIC_FIELDS[8].bytes, UINT64_C(8));
    CHECK_U(MESH_NIC_FIELDS[9].off, UINT64_C(80));
    CHECK_U(MESH_NIC_FIELDS[9].bytes, UINT64_C(4));
    CHECK_U(MESH_NIC_FIELDS[10].off, UINT64_C(84));
    CHECK_U(MESH_NIC_FIELDS[10].bytes, UINT64_C(4));
    CHECK(mesh_nic_virtual((const uint8_t *)"lo", 2)); /* nic_virtual(b"lo") */
    CHECK(mesh_nic_virtual((const uint8_t *)"veth12ab", 8)); /* nic_virtual(b"veth12ab") */
    CHECK(mesh_nic_virtual((const uint8_t *)"br-8c53", 7)); /* nic_virtual(b"br-8c53") */
    CHECK(!mesh_nic_virtual((const uint8_t *)"enP7s7", 6)); /* !nic_virtual(b"enP7s7") */
    CHECK(!mesh_nic_virtual((const uint8_t *)"en0", 3)); /* !nic_virtual(b"en0") */
    CHECK(!mesh_nic_virtual((const uint8_t *)"l", 1)); /* !nic_virtual(b"l") */
    CHECK(mesh_nic_virtual((const uint8_t *)"stf0", 4)); /* nic_virtual(b"stf0") */
    return mesh_check_done();
}
