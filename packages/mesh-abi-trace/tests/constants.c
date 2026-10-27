/* Every constant of @ajax/mesh-abi-trace equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/trace.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_TRACE_BASE, UINT64_C(106496));
    CHECK_U(MESH_TRACE_PER_PROG, UINT64_C(128));
    CHECK_U(MESH_TRACE_BYTES, UINT64_C(16));
    return mesh_check_done();
}
