/* @ajax/mesh-abi-host-sched's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_sched.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_placed(0), 5792u);
    CHECK_U(mesh_placed(1), 5796u);
    CHECK_U(mesh_placed(2), 5800u);
    CHECK_U(mesh_placed(7), 5820u);
    CHECK_U(mesh_placed(31), 5916u);
    CHECK_U(mesh_placed(32), 5920u);
    return mesh_check_done();
}
