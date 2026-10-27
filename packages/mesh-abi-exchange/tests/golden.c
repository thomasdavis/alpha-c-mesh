/* @ajax/mesh-abi-exchange's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/exchange.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_ex_counter(0), 172032u);
    CHECK_U(mesh_ex_slots(0), 172288u);
    CHECK_U(mesh_ex_counter(1), 172040u);
    CHECK_U(mesh_ex_slots(1), 172352u);
    CHECK_U(mesh_ex_counter(3), 172056u);
    CHECK_U(mesh_ex_slots(3), 172480u);
    CHECK_U(mesh_ex_counter(7), 172088u);
    CHECK_U(mesh_ex_slots(7), 172736u);
    CHECK_U(mesh_ex_counter(8), 172096u);
    CHECK_U(mesh_ex_slots(8), 172800u);
    return mesh_check_done();
}
