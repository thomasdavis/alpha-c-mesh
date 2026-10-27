/* @ajax/mesh-abi-wasm's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/wasm.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_wasm_w_peer(0), 256u);
    CHECK_U(mesh_wasm_w_peer(1), 257u);
    CHECK_U(mesh_wasm_w_peer(2), 258u);
    CHECK_U(mesh_wasm_w_peer(7), 263u);
    CHECK_U(mesh_wasm_w_peer(31), 287u);
    CHECK_U(mesh_wasm_w_peer(32), 288u);
    return mesh_check_done();
}
