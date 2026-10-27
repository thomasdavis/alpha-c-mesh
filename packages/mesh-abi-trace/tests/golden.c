/* @ajax/mesh-abi-trace's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/trace.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_trace_rec(0, 0), 106496u);
    CHECK_U(mesh_trace_rec(0, 1), 106512u);
    CHECK_U(mesh_trace_rec(0, 5), 106576u);
    CHECK_U(mesh_trace_rec(0, 31), 106992u);
    CHECK_U(mesh_trace_rec(0, 127), 108528u);
    CHECK_U(mesh_trace_rec(0, 128), 106496u);
    CHECK_U(mesh_trace_rec(0, 129), 106512u);
    CHECK_U(mesh_trace_rec(0, 1000), 108160u);
    CHECK_U(mesh_trace_rec(1, 0), 108544u);
    CHECK_U(mesh_trace_rec(1, 1), 108560u);
    CHECK_U(mesh_trace_rec(1, 5), 108624u);
    CHECK_U(mesh_trace_rec(1, 31), 109040u);
    CHECK_U(mesh_trace_rec(1, 127), 110576u);
    CHECK_U(mesh_trace_rec(1, 128), 108544u);
    CHECK_U(mesh_trace_rec(1, 129), 108560u);
    CHECK_U(mesh_trace_rec(1, 1000), 110208u);
    CHECK_U(mesh_trace_rec(2, 0), 110592u);
    CHECK_U(mesh_trace_rec(2, 1), 110608u);
    CHECK_U(mesh_trace_rec(2, 5), 110672u);
    CHECK_U(mesh_trace_rec(2, 31), 111088u);
    CHECK_U(mesh_trace_rec(2, 127), 112624u);
    CHECK_U(mesh_trace_rec(2, 128), 110592u);
    CHECK_U(mesh_trace_rec(2, 129), 110608u);
    CHECK_U(mesh_trace_rec(2, 1000), 112256u);
    CHECK_U(mesh_trace_rec(7, 0), 120832u);
    CHECK_U(mesh_trace_rec(7, 1), 120848u);
    CHECK_U(mesh_trace_rec(7, 5), 120912u);
    CHECK_U(mesh_trace_rec(7, 31), 121328u);
    CHECK_U(mesh_trace_rec(7, 127), 122864u);
    CHECK_U(mesh_trace_rec(7, 128), 120832u);
    CHECK_U(mesh_trace_rec(7, 129), 120848u);
    CHECK_U(mesh_trace_rec(7, 1000), 122496u);
    CHECK_U(mesh_trace_rec(31, 0), 169984u);
    CHECK_U(mesh_trace_rec(31, 1), 170000u);
    CHECK_U(mesh_trace_rec(31, 5), 170064u);
    CHECK_U(mesh_trace_rec(31, 31), 170480u);
    CHECK_U(mesh_trace_rec(31, 127), 172016u);
    CHECK_U(mesh_trace_rec(31, 128), 169984u);
    CHECK_U(mesh_trace_rec(31, 129), 170000u);
    CHECK_U(mesh_trace_rec(31, 1000), 171648u);
    CHECK_U(mesh_trace_rec(32, 0), 172032u);
    CHECK_U(mesh_trace_rec(32, 1), 172048u);
    CHECK_U(mesh_trace_rec(32, 5), 172112u);
    CHECK_U(mesh_trace_rec(32, 31), 172528u);
    CHECK_U(mesh_trace_rec(32, 127), 174064u);
    CHECK_U(mesh_trace_rec(32, 128), 172032u);
    CHECK_U(mesh_trace_rec(32, 129), 172048u);
    CHECK_U(mesh_trace_rec(32, 1000), 173696u);
    return mesh_check_done();
}
