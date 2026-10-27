/* Every constant of @ajax/mesh-abi-native equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/native.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_NATIVE_ABI, UINT64_C(1));
    CHECK_S(MESH_NATIVE_ENV_ABI, "MESH_NATIVE_ABI");
    CHECK_S(MESH_NATIVE_ENV_NAME, "MESH_PROGRAM");
    CHECK_I(MESH_NATIVE_STATUS_FD, 3);
    CHECK_I(MESH_NATIVE_MAILBOX_FD, 4);
    return mesh_check_done();
}
