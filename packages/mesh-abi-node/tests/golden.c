/* @ajax/mesh-abi-node's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/node.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_node_rec(0, 0), 73728u);
    CHECK_U(mesh_node_rec(0, 1), 73760u);
    CHECK_U(mesh_node_rec(0, 31), 74720u);
    CHECK_U(mesh_node_rec(1, 0), 74752u);
    CHECK_U(mesh_node_rec(1, 1), 74784u);
    CHECK_U(mesh_node_rec(1, 31), 75744u);
    CHECK_U(mesh_node_rec(2, 0), 75776u);
    CHECK_U(mesh_node_rec(2, 1), 75808u);
    CHECK_U(mesh_node_rec(2, 31), 76768u);
    CHECK_U(mesh_node_rec(7, 0), 80896u);
    CHECK_U(mesh_node_rec(7, 1), 80928u);
    CHECK_U(mesh_node_rec(7, 31), 81888u);
    CHECK_U(mesh_node_rec(31, 0), 105472u);
    CHECK_U(mesh_node_rec(31, 1), 105504u);
    CHECK_U(mesh_node_rec(31, 31), 106464u);
    CHECK_U(mesh_node_rec(32, 0), 106496u);
    CHECK_U(mesh_node_rec(32, 1), 106528u);
    CHECK_U(mesh_node_rec(32, 31), 107488u);
    return mesh_check_done();
}
