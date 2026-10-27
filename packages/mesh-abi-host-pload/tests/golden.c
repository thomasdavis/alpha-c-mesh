/* @ajax/mesh-abi-host-pload's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_pload.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_pload(0), 6144u);
    CHECK_U(mesh_pload(1), 6272u);
    CHECK_U(mesh_pload(2), 6400u);
    CHECK_U(mesh_pload(7), 7040u);
    CHECK_U(mesh_pload(31), 10112u);
    CHECK_U(mesh_pload(32), 10240u);
    return mesh_check_done();
}
