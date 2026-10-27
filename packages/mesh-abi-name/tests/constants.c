/* Every constant of @ajax/mesh-abi-name equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/name.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK(mesh_valid_name((const uint8_t *)"hello", 5)); /* valid_name(b"hello") */
    CHECK(mesh_valid_name((const uint8_t *)"a-1", 3)); /* valid_name(b"a-1") */
    CHECK(!mesh_valid_name((const uint8_t *)"", 0)); /* !valid_name(b"") */
    CHECK(!mesh_valid_name((const uint8_t *)"Hello", 5)); /* !valid_name(b"Hello") */
    CHECK(!mesh_valid_name((const uint8_t *)"0123456789abcdef", 16)); /* !valid_name(b"0123456789abcdef") */
    return mesh_check_done();
}
