/* Every constant of @ajax/mesh-abi-load-page equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/load_page.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_LOAD_PAGE, UINT64_C(294912));
    return mesh_check_done();
}
