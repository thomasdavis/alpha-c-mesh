/* @ajax/mesh-abi-ext's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/ext.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_prog_ext(0), 196608u);
    CHECK_U(mesh_prog_ext(1), 197632u);
    CHECK_U(mesh_prog_ext(2), 198656u);
    CHECK_U(mesh_prog_ext(7), 203776u);
    CHECK_U(mesh_prog_ext(31), 228352u);
    CHECK_U(mesh_prog_ext(32), 229376u);
    return mesh_check_done();
}
