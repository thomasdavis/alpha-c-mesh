/* @ajax/mesh-abi-host-mload's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_mload.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_mload(0), 4608u);
    CHECK_U(mesh_mload(1), 4672u);
    CHECK_U(mesh_mload(3), 4800u);
    CHECK_U(mesh_mload(7), 5056u);
    CHECK_U(mesh_mload(8), 5120u);
    return mesh_check_done();
}
