/* The Rust's pipedemo_tests::sha256d_of_a_known_message, ported. */
#include "mesh/abi/view_pipedemo.h"
#include "hist_quantile.h"
#include "mesh/check.h"

int main(void) {
    /* sha256(sha256(64 zero bytes)) = e2f61c3f71d1defd... (Python hashlib). */
    uint32_t zero[16] = {0}, d[8], m[16];
    mesh_pipedemo_sha256d_msg(zero, d);
    CHECK_U(d[0], 0xe2f61c3fu);
    CHECK_U(d[1], 0x71d1defdu);
    CHECK_U(d[7], 0x974e8cf9u);
    mesh_pipedemo_msg(7, UINT64_C(0x100000002), MESH_PIPEDEMO_ORIGIN_WASM, m);
    CHECK_U(m[MESH_PIPEDEMO_R_SEQ], 7);
    CHECK_U(m[MESH_PIPEDEMO_R_STAMP_LO], 2);
    CHECK_U(m[MESH_PIPEDEMO_R_STAMP_HI], 1);
    CHECK_U(m[MESH_PIPEDEMO_R_ORIGIN], 1);
    CHECK_U(m[15], mesh_pipedemo_payload(7, 15));
    const uint32_t h1[4] = {0, 2, 1, 1}, h0[4] = {0};
    CHECK_U(hist_quantile(h1, 4, 500), 1);
    CHECK_U(hist_quantile(h1, 4, 990), 3);
    CHECK_U(hist_quantile(h0, 4, 500), 0);
    return mesh_check_done();
}
