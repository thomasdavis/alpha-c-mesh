/* Every constant of @ajax/mesh-abi-control-protocol equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/control_protocol.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_CONTROL_PORT, UINT64_C(7700));
    CHECK_U(MESH_HTTP_PORT, UINT64_C(8088));
    CHECK_U(MESH_LOAD_REASONS_LEN, 13);
    CHECK_S(MESH_LOAD_REASONS[0], "manifest");
    CHECK_S(MESH_LOAD_REASONS[1], "abi");
    CHECK_S(MESH_LOAD_REASONS[2], "name exists");
    CHECK_S(MESH_LOAD_REASONS[3], "no slot");
    CHECK_S(MESH_LOAD_REASONS[4], "view heap");
    CHECK_S(MESH_LOAD_REASONS[5], "occupancy");
    CHECK_S(MESH_LOAD_REASONS[6], "regs");
    CHECK_S(MESH_LOAD_REASONS[7], "jit");
    CHECK_S(MESH_LOAD_REASONS[8], "load");
    CHECK_S(MESH_LOAD_REASONS[9], "peer");
    CHECK_S(MESH_LOAD_REASONS[10], "unsupported");
    CHECK_S(MESH_LOAD_REASONS[11], "float");
    CHECK_S(MESH_LOAD_REASONS[12], "exec");
    return mesh_check_done();
}
