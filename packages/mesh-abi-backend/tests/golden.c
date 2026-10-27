/* @ajax/mesh-abi-backend's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/backend.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_backend_word(0u, 0u), 0u);
    CHECK_U(mesh_backend_kind(0u), 0u);
    CHECK_U(mesh_backend_family(0u), 0u);
    CHECK_U(mesh_backend_word(1u, 9u), 2305u);
    CHECK_U(mesh_backend_kind(2305u), 1u);
    CHECK_U(mesh_backend_family(2305u), 9u);
    CHECK_U(mesh_backend_word(2u, 0u), 2u);
    CHECK_U(mesh_backend_kind(2u), 2u);
    CHECK_U(mesh_backend_family(2u), 0u);
    CHECK_U(mesh_backend_word(3u, 255u), 65283u);
    CHECK_U(mesh_backend_kind(65283u), 3u);
    CHECK_U(mesh_backend_family(65283u), 255u);
    CHECK_U(mesh_backend_word(511u, 511u), 65535u);
    CHECK_U(mesh_backend_kind(65535u), 255u);
    CHECK_U(mesh_backend_family(65535u), 255u);
    return mesh_check_done();
}
