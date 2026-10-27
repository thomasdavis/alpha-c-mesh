/* Every constant of @ajax/mesh-abi-base equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/base.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_U(MESH_MAGIC, UINT64_C(1213416781));
    CHECK_U(MESH_NODE_THREADS, UINT64_C(32));
    CHECK_U(MESH_MAX_WARPS, UINT64_C(32));
    CHECK_U(MESH_RT_SHARED_BYTES, UINT64_C(512));
    CHECK_U(MESH_MAX_SHARED_BYTES, UINT64_C(228864));
    CHECK_U(MESH_SHARED_ALIGN, UINT64_C(16));
    CHECK_U(MESH_PEER_NONE, UINT64_C(4294967295));
    CHECK_U(MESH_VERSION, UINT64_C(2));
    CHECK_U(MESH_RT_ABI, UINT64_C(6));
    CHECK_U(MESH_MAILBOX_BYTES, UINT64_C(8388608));
    CHECK_S(MESH_PROGRAM_ENTRY, "mesh_main");
    CHECK_S(MESH_RT_ABI_SYMBOL, "MESH_RT_ABI");
    CHECK_U(MESH_MAX_SYSTEMS, UINT64_C(8));
    CHECK_U(MESH_PROG_SLOTS, UINT64_C(32));
    CHECK_U(MESH_NAME_BYTES, UINT64_C(16));
    CHECK_U(MESH_NAME_MAX, UINT64_C(15));
    return mesh_check_done();
}
