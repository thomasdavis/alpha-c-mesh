/* @ajax/mesh-abi-placement's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/placement.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_show_bytes(0), 256u);
    CHECK_U(mesh_show_bytes(1), 258u);
    CHECK_U(mesh_show_bytes(512), 1280u);
    CHECK_U(mesh_show_bytes(1600), 3456u);
    CHECK_U(mesh_show_bytes(65535), 131326u);
    return mesh_check_done();
}
