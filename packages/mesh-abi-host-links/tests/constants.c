/* Every constant of @ajax/mesh-abi-host-links equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/host_links.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_S_LINKS, UINT64_C(1792));
    CHECK_U(MESH_LINK_BYTES, UINT64_C(128));
    CHECK_U(MESH_L_RX_BYTES, UINT64_C(0));
    CHECK_U(MESH_L_TX_BYTES, UINT64_C(8));
    CHECK_U(MESH_L_RX_FRAMES, UINT64_C(16));
    CHECK_U(MESH_L_TX_FRAMES, UINT64_C(48));
    CHECK_U(MESH_L_LOCAL, UINT64_C(80));
    CHECK_U(MESH_L_IFACE, UINT64_C(112));
    CHECK_U(MESH_NET_KINDS, UINT64_C(4));
    CHECK_U(MESH_KIND_ROUND, UINT64_C(0));
    CHECK_U(MESH_KIND_PLACE, UINT64_C(1));
    CHECK_U(MESH_KIND_LOAD, UINT64_C(2));
    CHECK_U(MESH_KIND_CTL, UINT64_C(3));
    CHECK_U(MESH_KIND_NAMES_LEN, 4);
    CHECK_S(MESH_KIND_NAMES[0], "round");
    CHECK_S(MESH_KIND_NAMES[1], "place");
    CHECK_S(MESH_KIND_NAMES[2], "state");
    CHECK_S(MESH_KIND_NAMES[3], "ctl");
    CHECK_U(MESH_LINK_FIELDS_LEN, 6);
    CHECK_U(MESH_LINK_FIELDS[0].off, UINT64_C(0));
    CHECK_U(MESH_LINK_FIELDS[0].bytes, UINT64_C(8));
    CHECK_U(MESH_LINK_FIELDS[1].off, UINT64_C(8));
    CHECK_U(MESH_LINK_FIELDS[1].bytes, UINT64_C(8));
    CHECK_U(MESH_LINK_FIELDS[2].off, UINT64_C(16));
    CHECK_U(MESH_LINK_FIELDS[2].bytes, UINT64_C(32));
    CHECK_U(MESH_LINK_FIELDS[3].off, UINT64_C(48));
    CHECK_U(MESH_LINK_FIELDS[3].bytes, UINT64_C(32));
    CHECK_U(MESH_LINK_FIELDS[4].off, UINT64_C(80));
    CHECK_U(MESH_LINK_FIELDS[4].bytes, UINT64_C(32));
    CHECK_U(MESH_LINK_FIELDS[5].off, UINT64_C(112));
    CHECK_U(MESH_LINK_FIELDS[5].bytes, UINT64_C(16));
    return mesh_check_done();
}
