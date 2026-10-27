/* Every constant of @ajax/mesh-abi-pipe equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/pipe.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_I(MESH_PIPE_OK, 1);
    CHECK_I(MESH_PIPE_NONE, 0);
    CHECK_I(MESH_PIPE_NOT_CONNECTED, -1);
    CHECK_I(MESH_PIPE_SIZE, -2);
    CHECK_I(MESH_PIPE_GONE, -3);
    return mesh_check_done();
}
