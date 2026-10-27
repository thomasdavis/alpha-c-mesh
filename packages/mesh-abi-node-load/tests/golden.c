/* @ajax/mesh-abi-node-load's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/node_load.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_node_load(0, 0), 262144u);
    CHECK_U(mesh_node_load(0, 1), 262176u);
    CHECK_U(mesh_node_load(0, 31), 263136u);
    CHECK_U(mesh_node_load(1, 0), 263168u);
    CHECK_U(mesh_node_load(1, 1), 263200u);
    CHECK_U(mesh_node_load(1, 31), 264160u);
    CHECK_U(mesh_node_load(2, 0), 264192u);
    CHECK_U(mesh_node_load(2, 1), 264224u);
    CHECK_U(mesh_node_load(2, 31), 265184u);
    CHECK_U(mesh_node_load(7, 0), 269312u);
    CHECK_U(mesh_node_load(7, 1), 269344u);
    CHECK_U(mesh_node_load(7, 31), 270304u);
    CHECK_U(mesh_node_load(31, 0), 293888u);
    CHECK_U(mesh_node_load(31, 1), 293920u);
    CHECK_U(mesh_node_load(31, 31), 294880u);
    CHECK_U(mesh_node_load(32, 0), 294912u);
    CHECK_U(mesh_node_load(32, 1), 294944u);
    CHECK_U(mesh_node_load(32, 31), 295904u);
    return mesh_check_done();
}
