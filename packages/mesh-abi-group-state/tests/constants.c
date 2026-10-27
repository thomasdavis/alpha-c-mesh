/* Every constant of @ajax/mesh-abi-group-state equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/group_state.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_GL_NONE, UINT64_C(0));
    CHECK_U(MESH_GL_PAUSED, UINT64_C(1));
    CHECK_U(MESH_GL_HELLO, UINT64_C(2));
    CHECK_U(MESH_GL_RUNNING, UINT64_C(3));
    CHECK_U(MESH_GL_NAMES_LEN, 4);
    CHECK_S(MESH_GL_NAMES[0], "-");
    CHECK_S(MESH_GL_NAMES[1], "paused");
    CHECK_S(MESH_GL_NAMES[2], "hello");
    CHECK_S(MESH_GL_NAMES[3], "running");
    return mesh_check_done();
}
