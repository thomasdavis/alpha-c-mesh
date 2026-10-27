/* Every constant of @ajax/mesh-abi-host-sched equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/host_sched.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_S_SCHED, UINT64_C(5760));
    CHECK_U(MESH_S_SCHED_FLAGS, UINT64_C(5760));
    CHECK_U(MESH_S_SCHED_MOVES, UINT64_C(5764));
    CHECK_U(MESH_S_SCHED_GAP_US, UINT64_C(5768));
    CHECK_U(MESH_S_CORDON, UINT64_C(5772));
    CHECK_U(MESH_S_BUSY_MASK, UINT64_C(5776));
    CHECK_U(MESH_S_HEAP_RUN, UINT64_C(5780));
    CHECK_U(MESH_S_PLACED, UINT64_C(5792));
    CHECK_U(MESH_PS_MASK, UINT64_C(255));
    CHECK_U(MESH_PS_ALL, UINT64_C(256));
    CHECK_U(MESH_PS_PINNED, UINT64_C(512));
    CHECK_U(MESH_PS_MIGRATING, UINT64_C(1024));
    CHECK_U(MESH_PS_KNOWN, UINT64_C(2048));
    CHECK_U(MESH_PS_FROM_SHIFT, UINT64_C(16));
    CHECK_U(MESH_SF_PAUSED, UINT64_C(1));
    CHECK_U(MESH_SF_AUTOREFORM, UINT64_C(2));
    CHECK_U(MESH_SF_MIGRATING, UINT64_C(4));
    CHECK_U(MESH_S_SCHED_END, UINT64_C(5920));
    return mesh_check_done();
}
