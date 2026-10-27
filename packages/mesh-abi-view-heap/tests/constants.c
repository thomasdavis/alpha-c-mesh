/* Every constant of @ajax/mesh-abi-view-heap equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/view_heap.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_VIEW_HEAP, UINT64_C(1048576));
    CHECK_U(MESH_VIEW_HEAP_END, UINT64_C(8388608));
    CHECK_U(MESH_VIEW_PAGE, UINT64_C(4096));
    CHECK_U(MESH_VIEW_PAGES, UINT64_C(1792));
    return mesh_check_done();
}
