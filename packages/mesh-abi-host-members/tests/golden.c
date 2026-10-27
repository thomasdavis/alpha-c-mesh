/* @ajax/mesh-abi-host-members's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_members.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_member(0), 576u);
    CHECK_U(mesh_member(1), 704u);
    CHECK_U(mesh_member(3), 960u);
    CHECK_U(mesh_member(7), 1472u);
    CHECK_U(mesh_member(8), 1600u);
    return mesh_check_done();
}
