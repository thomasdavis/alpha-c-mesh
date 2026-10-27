/* Every constant of @ajax/mesh-abi-exchange equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/exchange.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_EX_BASE, UINT64_C(172032));
    CHECK_U(MESH_EX_BYTES, UINT64_C(4096));
    CHECK_U(MESH_EX_SLOT_BYTES, UINT64_C(32));
    CHECK_U(MESH_EX_REGION_BYTES, UINT64_C(64));
    CHECK_U(MESH_PEER_POLLS, UINT64_C(50000));
    CHECK_U(MESH_PEER_POLL_NS, UINT64_C(2000));
    return mesh_check_done();
}
