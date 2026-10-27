/* Every constant of @ajax/mesh-abi-host-jobs equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/host_jobs.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_S_JOBS, UINT64_C(5952));
    CHECK_U(MESH_S_JOBS_ACTIVE, UINT64_C(5952));
    CHECK_U(MESH_S_JOBS_TOTAL, UINT64_C(5956));
    CHECK_U(MESH_S_JOB_DONE, UINT64_C(5960));
    CHECK_U(MESH_S_JOB_FAILED, UINT64_C(5968));
    CHECK_U(MESH_S_JOB_LEASED, UINT64_C(5976));
    CHECK_U(MESH_S_JOB_RATE, UINT64_C(5980));
    CHECK_U(MESH_S_JOB_DUPES, UINT64_C(5984));
    CHECK_U(MESH_S_JOB_COMMIT, UINT64_C(5992));
    CHECK_U(MESH_S_JOB_TERM, UINT64_C(6000));
    CHECK_U(MESH_S_JOB_FLAGS, UINT64_C(6008));
    CHECK_U(MESH_S_JOB_HELD, UINT64_C(6012));
    CHECK_U(MESH_S_JOBS_END, UINT64_C(6016));
    CHECK_U(MESH_JF_LEADER, UINT64_C(1));
    CHECK_U(MESH_JF_ACTIVE, UINT64_C(2));
    CHECK_U(MESH_JF_PERSIST, UINT64_C(4));
    return mesh_check_done();
}
