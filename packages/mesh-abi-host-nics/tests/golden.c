/* @ajax/mesh-abi-host-nics's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_nics.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_nic(0), 2816u);
    CHECK_U(mesh_nic(1), 2912u);
    CHECK_U(mesh_nic(3), 3104u);
    CHECK_U(mesh_nic(7), 3488u);
    CHECK_U(mesh_nic(8), 3584u);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"lo", 2), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"lo0", 3), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"l", 1), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"", 0), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"eth0", 4), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"enP7s7", 6), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"en0", 3), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"docker0", 7), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"br-8c53", 7), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"bridge0", 7), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"virbr0", 6), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"lxdbr0", 6), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"veth12ab", 8), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"cni0", 4), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"flannel.1", 9), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"vmenet0", 7), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"tailscale0", 10), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"utun3", 5), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"tun0", 4), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"wg0", 3), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"gif0", 4), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"stf0", 4), 1);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"wlan0", 5), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"bond0", 5), 0);
    CHECK_U(mesh_nic_virtual((const uint8_t *)"ib0", 3), 0);
    return mesh_check_done();
}
