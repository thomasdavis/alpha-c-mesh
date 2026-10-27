/* @ajax/mesh-abi-control-block's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/control_block.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_prog_ctl(0), 8192u);
    CHECK_U(mesh_prog_ctl(1), 10240u);
    CHECK_U(mesh_prog_ctl(2), 12288u);
    CHECK_U(mesh_prog_ctl(7), 22528u);
    CHECK_U(mesh_prog_ctl(31), 71680u);
    CHECK_U(mesh_prog_ctl(32), 73728u);
    CHECK_U(mesh_run_req_next(0u, false), 2u);
    CHECK_U(mesh_run_req_next(0u, true), 3u);
    CHECK_U(mesh_run_req_next(1u, false), 2u);
    CHECK_U(mesh_run_req_next(1u, true), 3u);
    CHECK_U(mesh_run_req_next(2u, false), 4u);
    CHECK_U(mesh_run_req_next(2u, true), 5u);
    CHECK_U(mesh_run_req_next(3u, false), 4u);
    CHECK_U(mesh_run_req_next(3u, true), 5u);
    CHECK_U(mesh_run_req_next(4294967294u, false), 0u);
    CHECK_U(mesh_run_req_next(4294967294u, true), 1u);
    CHECK_U(mesh_run_req_next(4294967295u, false), 0u);
    CHECK_U(mesh_run_req_next(4294967295u, true), 1u);
    return mesh_check_done();
}
