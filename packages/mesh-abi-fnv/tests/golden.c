/* @ajax/mesh-abi-fnv's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/fnv.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_fnv1a64((const uint8_t *)"", 0), UINT64_C(14695981039346656037));
    CHECK_U(mesh_fnv1a64((const uint8_t *)"a", 1), UINT64_C(12638187200555641996));
    CHECK_U(mesh_fnv1a64((const uint8_t *)"foobar", 6), UINT64_C(9625390261332436968));
    CHECK_U(mesh_fnv1a64((const uint8_t *)"mesh_main", 9), UINT64_C(1441023799754377036));
    CHECK_U(mesh_fnv1a64((const uint8_t *)"hello", 5), UINT64_C(11831194018420276491));
    CHECK_U(mesh_fnv1a64((const uint8_t *)"pipehash", 8), UINT64_C(11885407170553203205));
    return mesh_check_done();
}
