/* @ajax/mesh-abi-pipe-table's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/pipe_table.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_pipe_entry(0), 303104u);
    CHECK_U(mesh_flight_word(0, 0), 303132u);
    CHECK_U(mesh_flight_word(0, 1), 303152u);
    CHECK_U(mesh_flight_word(0, 2), 303132u);
    CHECK_U(mesh_flight_word(0, 3), 303152u);
    CHECK_U(mesh_pipe_entry(1), 303168u);
    CHECK_U(mesh_flight_word(1, 0), 303196u);
    CHECK_U(mesh_flight_word(1, 1), 303216u);
    CHECK_U(mesh_flight_word(1, 2), 303196u);
    CHECK_U(mesh_flight_word(1, 3), 303216u);
    CHECK_U(mesh_pipe_entry(2), 303232u);
    CHECK_U(mesh_flight_word(2, 0), 303260u);
    CHECK_U(mesh_flight_word(2, 1), 303280u);
    CHECK_U(mesh_flight_word(2, 2), 303260u);
    CHECK_U(mesh_flight_word(2, 3), 303280u);
    CHECK_U(mesh_pipe_entry(7), 303552u);
    CHECK_U(mesh_flight_word(7, 0), 303580u);
    CHECK_U(mesh_flight_word(7, 1), 303600u);
    CHECK_U(mesh_flight_word(7, 2), 303580u);
    CHECK_U(mesh_flight_word(7, 3), 303600u);
    CHECK_U(mesh_pipe_entry(31), 305088u);
    CHECK_U(mesh_flight_word(31, 0), 305116u);
    CHECK_U(mesh_flight_word(31, 1), 305136u);
    CHECK_U(mesh_flight_word(31, 2), 305116u);
    CHECK_U(mesh_flight_word(31, 3), 305136u);
    CHECK_U(mesh_pipe_entry(32), 305152u);
    CHECK_U(mesh_flight_word(32, 0), 305180u);
    CHECK_U(mesh_flight_word(32, 1), 305200u);
    CHECK_U(mesh_flight_word(32, 2), 305180u);
    CHECK_U(mesh_flight_word(32, 3), 305200u);
    return mesh_check_done();
}
