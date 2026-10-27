/* Every constant of @ajax/mesh-abi-wasm equals the Rust's (values printed by the Rust itself), and the
 * Rust const asserts that need a function call hold. Generated from alpha-rust-mesh. */
#include "mesh/abi/wasm.h"
#include "mesh/check.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    CHECK_S(MESH_WASM_MODULE, "mesh");
    CHECK_S(MESH_WASM_EXPORT_MEMORY, "memory");
    CHECK_S(MESH_WASM_EXPORT_INIT, "mesh_init");
    CHECK_S(MESH_WASM_EXPORT_TASK, "mesh_task");
    CHECK_S(MESH_WASM_EXPORT_RT_ABI, "mesh_rt_abi");
    CHECK_S(MESH_WASM_FTZ_PREFIX, "mesh_ftz_");
    CHECK_U(MESH_WASM_W_VIEW, UINT64_C(0));
    CHECK_U(MESH_WASM_W_EXCHANGE, UINT64_C(1));
    CHECK_U(MESH_WASM_W_PEER, UINT64_C(256));
    CHECK_U(MESH_WASM_I_SLOT, UINT64_C(0));
    CHECK_U(MESH_WASM_I_NODE, UINT64_C(1));
    CHECK_U(MESH_WASM_I_NODES, UINT64_C(2));
    CHECK_U(MESH_WASM_I_TICKET, UINT64_C(3));
    CHECK_U(MESH_WASM_I_SYSTEM, UINT64_C(4));
    CHECK_U(MESH_WASM_I_SYSTEMS, UINT64_C(5));
    CHECK_U(MESH_WASM_I_PARAM, UINT64_C(6));
    CHECK_U(MESH_WASM_I_SMS, UINT64_C(7));
    CHECK_U(MESH_WASM_I_VIEW_BYTES, UINT64_C(8));
    CHECK_U(MESH_WASM_I_REQ_LEN, UINT64_C(9));
    CHECK_U(MESH_WASM_I_REQ_OFF, UINT64_C(10));
    CHECK_U(MESH_WASM_I_RESP_OFF, UINT64_C(11));
    CHECK_U(MESH_WASM_I_RESP_MAX, UINT64_C(12));
    CHECK_U(MESH_WASM_I_INPUT_LEN, UINT64_C(13));
    CHECK_U(MESH_WASM_I_LOCKSTEP, UINT64_C(14));
    CHECK_U(MESH_WASM_I_READS_MASK, UINT64_C(15));
    CHECK_U(MESH_WASM_I_GROUP_RANK, UINT64_C(16));
    CHECK_U(MESH_WASM_I_GROUP_N, UINT64_C(17));
    return mesh_check_done();
}
