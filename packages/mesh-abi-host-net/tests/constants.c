/* Every constant of @ajax/mesh-abi-host-net equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/host_net.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_S_NET_AT_MS, UINT64_C(1600));
    CHECK_U(MESH_S_NET_NICS, UINT64_C(1608));
    CHECK_U(MESH_S_NET_SAMPLES, UINT64_C(1612));
    CHECK_U(MESH_S_CTL_RX, UINT64_C(1616));
    CHECK_U(MESH_S_CTL_TX, UINT64_C(1624));
    CHECK_U(MESH_S_CTL_REQS, UINT64_C(1632));
    CHECK_U(MESH_S_HTTP_RX, UINT64_C(1640));
    CHECK_U(MESH_S_HTTP_TX, UINT64_C(1648));
    CHECK_U(MESH_S_HTTP_CONNS, UINT64_C(1656));
    CHECK_U(MESH_S_NET_TOTAL, UINT64_C(1664));
    return mesh_check_done();
}
