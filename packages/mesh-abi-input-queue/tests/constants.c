/* Every constant of @ajax/mesh-abi-input-queue equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/input_queue.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_IQ_HEAD, UINT64_C(0));
    CHECK_U(MESH_IQ_TAIL, UINT64_C(4));
    CHECK_U(MESH_IQ_DATA, UINT64_C(16));
    return mesh_check_done();
}
