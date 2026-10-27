/* @ajax/mesh-abi-host-fleet's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_fleet.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_fmember(0), 5184u);
    CHECK_U(mesh_fmember(1), 5248u);
    CHECK_U(mesh_fmember(3), 5376u);
    CHECK_U(mesh_fmember(7), 5632u);
    CHECK_U(mesh_fmember(8), 5696u);
    return mesh_check_done();
}
