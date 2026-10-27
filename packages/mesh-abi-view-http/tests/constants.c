/* Every constant of @ajax/mesh-abi-view-http equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/view_http.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_HTTP_HV_SERVED, UINT64_C(0));
    CHECK_U(MESH_HTTP_HV_LAST_CODE, UINT64_C(4));
    CHECK_U(MESH_HTTP_HV_LOAD_SEQ, UINT64_C(8));
    CHECK_U(MESH_HTTP_HV_LOAD_ACK, UINT64_C(12));
    CHECK_U(MESH_HTTP_HV_LOAD_RESULT, UINT64_C(16));
    CHECK_U(MESH_HTTP_HV_LOAD_NAME, UINT64_C(20));
    CHECK_U(MESH_HTTP_HV_SAMPLES, UINT64_C(512));
    CHECK_U(MESH_HTTP_SAMPLE_BYTES, UINT64_C(16));
    CHECK_U(MESH_HTTP_REQ_OFF, UINT64_C(256));
    CHECK_U(MESH_HTTP_REQ_MAX, UINT64_C(256));
    CHECK_U(MESH_HTTP_RESP_OFF, UINT64_C(1024));
    CHECK_U(MESH_HTTP_RESP_MAX, UINT64_C(692224));
    CHECK_U(MESH_HTTP_VIEW_BYTES, UINT64_C(693248));
    CHECK_U(MESH_HTTP_LOAD_OK, UINT64_C(0));
    CHECK_U(MESH_HTTP_DOOM_ROWS, UINT64_C(24));
    return mesh_check_done();
}
