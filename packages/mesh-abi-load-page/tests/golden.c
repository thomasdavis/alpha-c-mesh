/* @ajax/mesh-abi-load-page's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/load_page.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_load_page(4096), 294912u);
    CHECK_U(mesh_load_page(4112), 294928u);
    CHECK_U(mesh_load_page(6144), 296960u);
    CHECK_U(mesh_load_page(10240), 301056u);
    return mesh_check_done();
}
