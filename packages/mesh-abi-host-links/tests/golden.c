/* @ajax/mesh-abi-host-links's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/host_links.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_link(0), 1792u);
    CHECK_U(mesh_link(1), 1920u);
    CHECK_U(mesh_link(3), 2176u);
    CHECK_U(mesh_link(7), 2688u);
    CHECK_U(mesh_link(8), 2816u);
    CHECK_U(mesh_frame_kind(0), 3u);
    CHECK_U(mesh_frame_kind(1), 0u);
    CHECK_U(mesh_frame_kind(2), 0u);
    CHECK_U(mesh_frame_kind(3), 3u);
    CHECK_U(mesh_frame_kind(4), 3u);
    CHECK_U(mesh_frame_kind(5), 3u);
    CHECK_U(mesh_frame_kind(6), 2u);
    CHECK_U(mesh_frame_kind(7), 2u);
    CHECK_U(mesh_frame_kind(8), 2u);
    CHECK_U(mesh_frame_kind(9), 1u);
    CHECK_U(mesh_frame_kind(10), 1u);
    CHECK_U(mesh_frame_kind(11), 1u);
    CHECK_U(mesh_frame_kind(12), 3u);
    CHECK_U(mesh_frame_kind(13), 3u);
    CHECK_U(mesh_frame_kind(14), 2u);
    CHECK_U(mesh_frame_kind(15), 2u);
    CHECK_U(mesh_frame_kind(16), 2u);
    CHECK_U(mesh_frame_kind(17), 2u);
    CHECK_U(mesh_frame_kind(18), 3u);
    CHECK_U(mesh_frame_kind(19), 2u);
    CHECK_U(mesh_frame_kind(20), 2u);
    CHECK_U(mesh_frame_kind(21), 2u);
    CHECK_U(mesh_frame_kind(22), 2u);
    CHECK_U(mesh_frame_kind(23), 2u);
    CHECK_U(mesh_frame_kind(24), 2u);
    CHECK_U(mesh_frame_kind(25), 2u);
    CHECK_U(mesh_frame_kind(26), 2u);
    CHECK_U(mesh_frame_kind(27), 2u);
    CHECK_U(mesh_frame_kind(28), 2u);
    CHECK_U(mesh_frame_kind(29), 2u);
    CHECK_U(mesh_frame_kind(30), 2u);
    CHECK_U(mesh_frame_kind(31), 2u);
    CHECK_U(mesh_frame_kind(32), 2u);
    CHECK_U(mesh_frame_kind(33), 3u);
    CHECK_U(mesh_frame_kind(34), 3u);
    CHECK_U(mesh_frame_kind(35), 3u);
    CHECK_U(mesh_frame_kind(36), 3u);
    CHECK_U(mesh_frame_kind(37), 3u);
    CHECK_U(mesh_frame_kind(38), 3u);
    CHECK_U(mesh_frame_kind(39), 3u);
    CHECK_U(mesh_frame_kind(40), 3u);
    return mesh_check_done();
}
