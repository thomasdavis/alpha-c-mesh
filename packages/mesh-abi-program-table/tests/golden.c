/* @ajax/mesh-abi-program-table's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/program_table.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_prog_entry(0), 4096u);
    CHECK_U(mesh_prog_entry(1), 4224u);
    CHECK_U(mesh_prog_entry(2), 4352u);
    CHECK_U(mesh_prog_entry(7), 4992u);
    CHECK_U(mesh_prog_entry(31), 8064u);
    CHECK_U(mesh_prog_entry(32), 8192u);
    return mesh_check_done();
}
