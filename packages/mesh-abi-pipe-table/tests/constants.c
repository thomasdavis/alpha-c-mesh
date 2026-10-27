/* Every constant of @ajax/mesh-abi-pipe-table equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/pipe_table.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_PIPE_TABLE, UINT64_C(303104));
    CHECK_U(MESH_PP_BYTES, UINT64_C(64));
    CHECK_U(MESH_PP_IN_OFF, UINT64_C(0));
    CHECK_U(MESH_PP_IN_CAP, UINT64_C(4));
    CHECK_U(MESH_PP_IN_REC, UINT64_C(8));
    CHECK_U(MESH_PP_IN_TYPE, UINT64_C(12));
    CHECK_U(MESH_PP_FEEDS_MASK, UINT64_C(16));
    CHECK_U(MESH_PP_FED_MASK, UINT64_C(20));
    CHECK_U(MESH_PP_IN_HOLD, UINT64_C(24));
    CHECK_U(MESH_PP_FLIGHT, UINT64_C(28));
    CHECK_U(MESH_PP_FLIGHT1, UINT64_C(48));
    CHECK_U(MESH_PP_TYPE_NAME, UINT64_C(32));
    CHECK_U(MESH_PIPE_FIELDS_LEN, 10);
    CHECK_U(MESH_PIPE_FIELDS[0].off, UINT64_C(0));
    CHECK_U(MESH_PIPE_FIELDS[0].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[1].off, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[1].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[2].off, UINT64_C(8));
    CHECK_U(MESH_PIPE_FIELDS[2].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[3].off, UINT64_C(12));
    CHECK_U(MESH_PIPE_FIELDS[3].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[4].off, UINT64_C(16));
    CHECK_U(MESH_PIPE_FIELDS[4].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[5].off, UINT64_C(20));
    CHECK_U(MESH_PIPE_FIELDS[5].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[6].off, UINT64_C(24));
    CHECK_U(MESH_PIPE_FIELDS[6].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[7].off, UINT64_C(28));
    CHECK_U(MESH_PIPE_FIELDS[7].bytes, UINT64_C(4));
    CHECK_U(MESH_PIPE_FIELDS[8].off, UINT64_C(32));
    CHECK_U(MESH_PIPE_FIELDS[8].bytes, UINT64_C(16));
    CHECK_U(MESH_PIPE_FIELDS[9].off, UINT64_C(48));
    CHECK_U(MESH_PIPE_FIELDS[9].bytes, UINT64_C(4));
    return mesh_check_done();
}
