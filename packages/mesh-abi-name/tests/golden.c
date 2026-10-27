/* @ajax/mesh-abi-name's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/name.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_valid_name((const uint8_t *)"", 0), 0);
    CHECK_U(mesh_valid_name((const uint8_t *)"a", 1), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"hello", 5), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"a-1", 3), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"Hello", 5), 0);
    CHECK_U(mesh_valid_name((const uint8_t *)"0123456789abcde", 15), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"0123456789abcdef", 16), 0);
    CHECK_U(mesh_valid_name((const uint8_t *)"doom", 4), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"gpt2x", 5), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"x_y", 3), 0);
    CHECK_U(mesh_valid_name((const uint8_t *)"a b", 3), 0);
    CHECK_U(mesh_valid_name((const uint8_t *)"-", 1), 1);
    CHECK_U(mesh_valid_name((const uint8_t *)"z9", 2), 1);
    return mesh_check_done();
}
