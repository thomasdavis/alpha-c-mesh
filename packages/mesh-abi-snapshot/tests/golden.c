/* @ajax/mesh-abi-snapshot's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/snapshot.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_section_bytes(0, 0), 1680u);
    CHECK_U(mesh_section_bytes(1, 1), 1716u);
    CHECK_U(mesh_section_bytes(2, 4), 1748u);
    CHECK_U(mesh_section_bytes(32, 4096), 6800u);
    CHECK_U(mesh_section_bytes(5, 3), 1844u);
    CHECK_U(mesh_section_bytes(31, 262144), 264816u);
    return mesh_check_done();
}
