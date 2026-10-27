/* Every constant of @ajax/mesh-abi-fnv equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/fnv.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK(mesh_fnv1a64((const uint8_t *)"", 0) == 0xcbf29ce484222325); /* fnv1a64(b"") == 0xcbf2_9ce4_8422_2325 */
    return mesh_check_done();
}
