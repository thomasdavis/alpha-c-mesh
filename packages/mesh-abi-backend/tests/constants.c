/* Every constant of @ajax/mesh-abi-backend equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/backend.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_BACKEND_CUDA, UINT64_C(0));
    CHECK_U(MESH_BACKEND_METAL, UINT64_C(1));
    CHECK_U(MESH_BACKEND_WASM, UINT64_C(2));
    CHECK_U(MESH_BACKEND_NATIVE, UINT64_C(3));
    CHECK_U(MESH_BACKENDS_LEN, 4);
    CHECK_S(MESH_BACKENDS[0], "cuda");
    CHECK_S(MESH_BACKENDS[1], "metal");
    CHECK_S(MESH_BACKENDS[2], "wasm");
    CHECK_S(MESH_BACKENDS[3], "native");
    CHECK(mesh_backend_kind(mesh_backend_word(MESH_BACKEND_METAL, 9)) == MESH_BACKEND_METAL); /* backend_kind(backend_word(BACKEND_METAL, 9)) == BACKEND_METAL */
    CHECK(mesh_backend_family(mesh_backend_word(MESH_BACKEND_METAL, 9)) == 9); /* backend_family(backend_word(BACKEND_METAL, 9)) == 9 */
    CHECK(mesh_backend_word(MESH_BACKEND_CUDA, 0) == 0); /* backend_word(BACKEND_CUDA, 0) == 0 */
    return mesh_check_done();
}
