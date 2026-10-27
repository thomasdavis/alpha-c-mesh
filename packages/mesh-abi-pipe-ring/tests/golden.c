/* @ajax/mesh-abi-pipe-ring's functions against vectors the Rust computed (alpha-rust-mesh, the same inputs). */
#include "mesh/abi/pipe_ring.h"
#include "mesh/check.h"

#include <stdint.h>

int main(void) {
    CHECK_U(mesh_pipe_owner_tag(0u), 2119671808u);
    CHECK_U(mesh_pipe_owner_tag(1u), 2119671809u);
    CHECK_U(mesh_pipe_owner_tag(5u), 2119671813u);
    CHECK_U(mesh_pipe_owner_tag(4095u), 2119675903u);
    CHECK_U(mesh_pipe_owner_tag(4096u), 2119671808u);
    CHECK_U(mesh_pipe_owner_tag(4294967295u), 2119675903u);
    CHECK_U(mesh_pipe_inbox_sums(2), 136u);
    CHECK_U(mesh_pipe_inbox_data(2), 144u);
    CHECK_U(mesh_pipe_ring_bytes(2, 4), 192u);
    CHECK_U(mesh_pipe_inbox_bytes(2, 4), 384u);
    CHECK_U(mesh_pipe_inbox_sums(2), 136u);
    CHECK_U(mesh_pipe_inbox_data(2), 144u);
    CHECK_U(mesh_pipe_ring_bytes(2, 8), 192u);
    CHECK_U(mesh_pipe_inbox_bytes(2, 8), 384u);
    CHECK_U(mesh_pipe_inbox_sums(2), 136u);
    CHECK_U(mesh_pipe_inbox_data(2), 144u);
    CHECK_U(mesh_pipe_ring_bytes(2, 32), 256u);
    CHECK_U(mesh_pipe_inbox_bytes(2, 32), 384u);
    CHECK_U(mesh_pipe_inbox_sums(2), 136u);
    CHECK_U(mesh_pipe_inbox_data(2), 144u);
    CHECK_U(mesh_pipe_ring_bytes(2, 48), 256u);
    CHECK_U(mesh_pipe_inbox_bytes(2, 48), 384u);
    CHECK_U(mesh_pipe_inbox_sums(2), 136u);
    CHECK_U(mesh_pipe_inbox_data(2), 144u);
    CHECK_U(mesh_pipe_ring_bytes(2, 64), 320u);
    CHECK_U(mesh_pipe_inbox_bytes(2, 64), 512u);
    CHECK_U(mesh_pipe_inbox_sums(2), 136u);
    CHECK_U(mesh_pipe_inbox_data(2), 144u);
    CHECK_U(mesh_pipe_ring_bytes(2, 4096), 8384u);
    CHECK_U(mesh_pipe_inbox_bytes(2, 4096), 8576u);
    CHECK_U(mesh_pipe_inbox_sums(4), 144u);
    CHECK_U(mesh_pipe_inbox_data(4), 160u);
    CHECK_U(mesh_pipe_ring_bytes(4, 4), 192u);
    CHECK_U(mesh_pipe_inbox_bytes(4, 4), 384u);
    CHECK_U(mesh_pipe_inbox_sums(4), 144u);
    CHECK_U(mesh_pipe_inbox_data(4), 160u);
    CHECK_U(mesh_pipe_ring_bytes(4, 8), 192u);
    CHECK_U(mesh_pipe_inbox_bytes(4, 8), 384u);
    CHECK_U(mesh_pipe_inbox_sums(4), 144u);
    CHECK_U(mesh_pipe_inbox_data(4), 160u);
    CHECK_U(mesh_pipe_ring_bytes(4, 32), 320u);
    CHECK_U(mesh_pipe_inbox_bytes(4, 32), 512u);
    CHECK_U(mesh_pipe_inbox_sums(4), 144u);
    CHECK_U(mesh_pipe_inbox_data(4), 160u);
    CHECK_U(mesh_pipe_ring_bytes(4, 48), 384u);
    CHECK_U(mesh_pipe_inbox_bytes(4, 48), 512u);
    CHECK_U(mesh_pipe_inbox_sums(4), 144u);
    CHECK_U(mesh_pipe_inbox_data(4), 160u);
    CHECK_U(mesh_pipe_ring_bytes(4, 64), 448u);
    CHECK_U(mesh_pipe_inbox_bytes(4, 64), 640u);
    CHECK_U(mesh_pipe_inbox_sums(4), 144u);
    CHECK_U(mesh_pipe_inbox_data(4), 160u);
    CHECK_U(mesh_pipe_ring_bytes(4, 4096), 16576u);
    CHECK_U(mesh_pipe_inbox_bytes(4, 4096), 16768u);
    CHECK_U(mesh_pipe_inbox_sums(8), 160u);
    CHECK_U(mesh_pipe_inbox_data(8), 192u);
    CHECK_U(mesh_pipe_ring_bytes(8, 4), 256u);
    CHECK_U(mesh_pipe_inbox_bytes(8, 4), 384u);
    CHECK_U(mesh_pipe_inbox_sums(8), 160u);
    CHECK_U(mesh_pipe_inbox_data(8), 192u);
    CHECK_U(mesh_pipe_ring_bytes(8, 8), 256u);
    CHECK_U(mesh_pipe_inbox_bytes(8, 8), 384u);
    CHECK_U(mesh_pipe_inbox_sums(8), 160u);
    CHECK_U(mesh_pipe_inbox_data(8), 192u);
    CHECK_U(mesh_pipe_ring_bytes(8, 32), 448u);
    CHECK_U(mesh_pipe_inbox_bytes(8, 32), 640u);
    CHECK_U(mesh_pipe_inbox_sums(8), 160u);
    CHECK_U(mesh_pipe_inbox_data(8), 192u);
    CHECK_U(mesh_pipe_ring_bytes(8, 48), 576u);
    CHECK_U(mesh_pipe_inbox_bytes(8, 48), 768u);
    CHECK_U(mesh_pipe_inbox_sums(8), 160u);
    CHECK_U(mesh_pipe_inbox_data(8), 192u);
    CHECK_U(mesh_pipe_ring_bytes(8, 64), 704u);
    CHECK_U(mesh_pipe_inbox_bytes(8, 64), 896u);
    CHECK_U(mesh_pipe_inbox_sums(8), 160u);
    CHECK_U(mesh_pipe_inbox_data(8), 192u);
    CHECK_U(mesh_pipe_ring_bytes(8, 4096), 32960u);
    CHECK_U(mesh_pipe_inbox_bytes(8, 4096), 33152u);
    CHECK_U(mesh_pipe_inbox_sums(64), 384u);
    CHECK_U(mesh_pipe_inbox_data(64), 640u);
    CHECK_U(mesh_pipe_ring_bytes(64, 4), 896u);
    CHECK_U(mesh_pipe_inbox_bytes(64, 4), 1024u);
    CHECK_U(mesh_pipe_inbox_sums(64), 384u);
    CHECK_U(mesh_pipe_inbox_data(64), 640u);
    CHECK_U(mesh_pipe_ring_bytes(64, 8), 1152u);
    CHECK_U(mesh_pipe_inbox_bytes(64, 8), 1280u);
    CHECK_U(mesh_pipe_inbox_sums(64), 384u);
    CHECK_U(mesh_pipe_inbox_data(64), 640u);
    CHECK_U(mesh_pipe_ring_bytes(64, 32), 2688u);
    CHECK_U(mesh_pipe_inbox_bytes(64, 32), 2816u);
    CHECK_U(mesh_pipe_inbox_sums(64), 384u);
    CHECK_U(mesh_pipe_inbox_data(64), 640u);
    CHECK_U(mesh_pipe_ring_bytes(64, 48), 3712u);
    CHECK_U(mesh_pipe_inbox_bytes(64, 48), 3840u);
    CHECK_U(mesh_pipe_inbox_sums(64), 384u);
    CHECK_U(mesh_pipe_inbox_data(64), 640u);
    CHECK_U(mesh_pipe_ring_bytes(64, 64), 4736u);
    CHECK_U(mesh_pipe_inbox_bytes(64, 64), 4864u);
    CHECK_U(mesh_pipe_inbox_sums(64), 384u);
    CHECK_U(mesh_pipe_inbox_data(64), 640u);
    CHECK_U(mesh_pipe_ring_bytes(64, 4096), 262784u);
    CHECK_U(mesh_pipe_inbox_bytes(64, 4096), 262912u);
    CHECK_U(mesh_pipe_inbox_sums(256), 1152u);
    CHECK_U(mesh_pipe_inbox_data(256), 2176u);
    CHECK_U(mesh_pipe_ring_bytes(256, 4), 3200u);
    CHECK_U(mesh_pipe_inbox_bytes(256, 4), 3328u);
    CHECK_U(mesh_pipe_inbox_sums(256), 1152u);
    CHECK_U(mesh_pipe_inbox_data(256), 2176u);
    CHECK_U(mesh_pipe_ring_bytes(256, 8), 4224u);
    CHECK_U(mesh_pipe_inbox_bytes(256, 8), 4352u);
    CHECK_U(mesh_pipe_inbox_sums(256), 1152u);
    CHECK_U(mesh_pipe_inbox_data(256), 2176u);
    CHECK_U(mesh_pipe_ring_bytes(256, 32), 10368u);
    CHECK_U(mesh_pipe_inbox_bytes(256, 32), 10496u);
    CHECK_U(mesh_pipe_inbox_sums(256), 1152u);
    CHECK_U(mesh_pipe_inbox_data(256), 2176u);
    CHECK_U(mesh_pipe_ring_bytes(256, 48), 14464u);
    CHECK_U(mesh_pipe_inbox_bytes(256, 48), 14592u);
    CHECK_U(mesh_pipe_inbox_sums(256), 1152u);
    CHECK_U(mesh_pipe_inbox_data(256), 2176u);
    CHECK_U(mesh_pipe_ring_bytes(256, 64), 18560u);
    CHECK_U(mesh_pipe_inbox_bytes(256, 64), 18688u);
    CHECK_U(mesh_pipe_inbox_sums(256), 1152u);
    CHECK_U(mesh_pipe_inbox_data(256), 2176u);
    CHECK_U(mesh_pipe_ring_bytes(256, 4096), 1050752u);
    CHECK_U(mesh_pipe_inbox_bytes(256, 4096), 1050880u);
    CHECK_U(mesh_pipe_inbox_sums(512), 2176u);
    CHECK_U(mesh_pipe_inbox_data(512), 4224u);
    CHECK_U(mesh_pipe_ring_bytes(512, 4), 6272u);
    CHECK_U(mesh_pipe_inbox_bytes(512, 4), 6400u);
    CHECK_U(mesh_pipe_inbox_sums(512), 2176u);
    CHECK_U(mesh_pipe_inbox_data(512), 4224u);
    CHECK_U(mesh_pipe_ring_bytes(512, 8), 8320u);
    CHECK_U(mesh_pipe_inbox_bytes(512, 8), 8448u);
    CHECK_U(mesh_pipe_inbox_sums(512), 2176u);
    CHECK_U(mesh_pipe_inbox_data(512), 4224u);
    CHECK_U(mesh_pipe_ring_bytes(512, 32), 20608u);
    CHECK_U(mesh_pipe_inbox_bytes(512, 32), 20736u);
    CHECK_U(mesh_pipe_inbox_sums(512), 2176u);
    CHECK_U(mesh_pipe_inbox_data(512), 4224u);
    CHECK_U(mesh_pipe_ring_bytes(512, 48), 28800u);
    CHECK_U(mesh_pipe_inbox_bytes(512, 48), 28928u);
    CHECK_U(mesh_pipe_inbox_sums(512), 2176u);
    CHECK_U(mesh_pipe_inbox_data(512), 4224u);
    CHECK_U(mesh_pipe_ring_bytes(512, 64), 36992u);
    CHECK_U(mesh_pipe_inbox_bytes(512, 64), 37120u);
    CHECK_U(mesh_pipe_inbox_sums(512), 2176u);
    CHECK_U(mesh_pipe_inbox_data(512), 4224u);
    CHECK_U(mesh_pipe_ring_bytes(512, 4096), 2101376u);
    CHECK_U(mesh_pipe_inbox_bytes(512, 4096), 2101504u);
    CHECK_U(mesh_pipe_inbox_sums(1024), 4224u);
    CHECK_U(mesh_pipe_inbox_data(1024), 8320u);
    CHECK_U(mesh_pipe_ring_bytes(1024, 4), 12416u);
    CHECK_U(mesh_pipe_inbox_bytes(1024, 4), 12544u);
    CHECK_U(mesh_pipe_inbox_sums(1024), 4224u);
    CHECK_U(mesh_pipe_inbox_data(1024), 8320u);
    CHECK_U(mesh_pipe_ring_bytes(1024, 8), 16512u);
    CHECK_U(mesh_pipe_inbox_bytes(1024, 8), 16640u);
    CHECK_U(mesh_pipe_inbox_sums(1024), 4224u);
    CHECK_U(mesh_pipe_inbox_data(1024), 8320u);
    CHECK_U(mesh_pipe_ring_bytes(1024, 32), 41088u);
    CHECK_U(mesh_pipe_inbox_bytes(1024, 32), 41216u);
    CHECK_U(mesh_pipe_inbox_sums(1024), 4224u);
    CHECK_U(mesh_pipe_inbox_data(1024), 8320u);
    CHECK_U(mesh_pipe_ring_bytes(1024, 48), 57472u);
    CHECK_U(mesh_pipe_inbox_bytes(1024, 48), 57600u);
    CHECK_U(mesh_pipe_inbox_sums(1024), 4224u);
    CHECK_U(mesh_pipe_inbox_data(1024), 8320u);
    CHECK_U(mesh_pipe_ring_bytes(1024, 64), 73856u);
    CHECK_U(mesh_pipe_inbox_bytes(1024, 64), 73984u);
    CHECK_U(mesh_pipe_inbox_sums(1024), 4224u);
    CHECK_U(mesh_pipe_inbox_data(1024), 8320u);
    CHECK_U(mesh_pipe_ring_bytes(1024, 4096), 4202624u);
    CHECK_U(mesh_pipe_inbox_bytes(1024, 4096), 4202752u);
    CHECK_U(mesh_pipe_inbox_sums(4096), 16512u);
    CHECK_U(mesh_pipe_inbox_data(4096), 32896u);
    CHECK_U(mesh_pipe_ring_bytes(4096, 4), 49280u);
    CHECK_U(mesh_pipe_inbox_bytes(4096, 4), 49408u);
    CHECK_U(mesh_pipe_inbox_sums(4096), 16512u);
    CHECK_U(mesh_pipe_inbox_data(4096), 32896u);
    CHECK_U(mesh_pipe_ring_bytes(4096, 8), 65664u);
    CHECK_U(mesh_pipe_inbox_bytes(4096, 8), 65792u);
    CHECK_U(mesh_pipe_inbox_sums(4096), 16512u);
    CHECK_U(mesh_pipe_inbox_data(4096), 32896u);
    CHECK_U(mesh_pipe_ring_bytes(4096, 32), 163968u);
    CHECK_U(mesh_pipe_inbox_bytes(4096, 32), 164096u);
    CHECK_U(mesh_pipe_inbox_sums(4096), 16512u);
    CHECK_U(mesh_pipe_inbox_data(4096), 32896u);
    CHECK_U(mesh_pipe_ring_bytes(4096, 48), 229504u);
    CHECK_U(mesh_pipe_inbox_bytes(4096, 48), 229632u);
    CHECK_U(mesh_pipe_inbox_sums(4096), 16512u);
    CHECK_U(mesh_pipe_inbox_data(4096), 32896u);
    CHECK_U(mesh_pipe_ring_bytes(4096, 64), 295040u);
    CHECK_U(mesh_pipe_inbox_bytes(4096, 64), 295168u);
    CHECK_U(mesh_pipe_inbox_sums(4096), 16512u);
    CHECK_U(mesh_pipe_inbox_data(4096), 32896u);
    CHECK_U(mesh_pipe_ring_bytes(4096, 4096), 16810112u);
    CHECK_U(mesh_pipe_inbox_bytes(4096, 4096), 16810240u);
    CHECK_U(mesh_pipe_inbox_sums(65536), 262272u);
    CHECK_U(mesh_pipe_inbox_data(65536), 524416u);
    CHECK_U(mesh_pipe_ring_bytes(65536, 4), 786560u);
    CHECK_U(mesh_pipe_inbox_bytes(65536, 4), 786688u);
    CHECK_U(mesh_pipe_inbox_sums(65536), 262272u);
    CHECK_U(mesh_pipe_inbox_data(65536), 524416u);
    CHECK_U(mesh_pipe_ring_bytes(65536, 8), 1048704u);
    CHECK_U(mesh_pipe_inbox_bytes(65536, 8), 1048832u);
    CHECK_U(mesh_pipe_inbox_sums(65536), 262272u);
    CHECK_U(mesh_pipe_inbox_data(65536), 524416u);
    CHECK_U(mesh_pipe_ring_bytes(65536, 32), 2621568u);
    CHECK_U(mesh_pipe_inbox_bytes(65536, 32), 2621696u);
    CHECK_U(mesh_pipe_inbox_sums(65536), 262272u);
    CHECK_U(mesh_pipe_inbox_data(65536), 524416u);
    CHECK_U(mesh_pipe_ring_bytes(65536, 48), 3670144u);
    CHECK_U(mesh_pipe_inbox_bytes(65536, 48), 3670272u);
    CHECK_U(mesh_pipe_inbox_sums(65536), 262272u);
    CHECK_U(mesh_pipe_inbox_data(65536), 524416u);
    CHECK_U(mesh_pipe_ring_bytes(65536, 64), 4718720u);
    CHECK_U(mesh_pipe_inbox_bytes(65536, 64), 4718848u);
    CHECK_U(mesh_pipe_inbox_sums(65536), 262272u);
    CHECK_U(mesh_pipe_inbox_data(65536), 524416u);
    CHECK_U(mesh_pipe_ring_bytes(65536, 4096), 268959872u);
    CHECK_U(mesh_pipe_inbox_bytes(65536, 4096), 268960000u);
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 0u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 0u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 0u), 392u); CHECK_U(mesh_pipe_ring_cell(&r, 0u), 400u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1u), 396u); CHECK_U(mesh_pipe_ring_cell(&r, 1u), 408u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1u), 396u); CHECK_U(mesh_pipe_ring_cell(&r, 1u), 408u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 2u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 2u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 2u), 392u); CHECK_U(mesh_pipe_ring_cell(&r, 2u), 400u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 3u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 3u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 3u), 396u); CHECK_U(mesh_pipe_ring_cell(&r, 3u), 408u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 8u), 8u); CHECK_U(mesh_pipe_ring_full(&r, 8u), 9u); CHECK_U(mesh_pipe_ring_sum(&r, 8u), 392u); CHECK_U(mesh_pipe_ring_cell(&r, 8u), 400u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 2147483647u), 2147483646u); CHECK_U(mesh_pipe_ring_full(&r, 2147483647u), 2147483647u); CHECK_U(mesh_pipe_ring_sum(&r, 2147483647u), 396u); CHECK_U(mesh_pipe_ring_cell(&r, 2147483647u), 408u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967294u), 4294967294u); CHECK_U(mesh_pipe_ring_full(&r, 4294967294u), 4294967295u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967294u), 392u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967294u), 400u); }
    { mesh_pipe_ring r = {0x100, 2, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967295u), 4294967294u); CHECK_U(mesh_pipe_ring_full(&r, 4294967295u), 4294967295u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967295u), 396u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967295u), 408u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 0u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 0u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 0u), 400u); CHECK_U(mesh_pipe_ring_cell(&r, 0u), 416u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1u), 404u); CHECK_U(mesh_pipe_ring_cell(&r, 1u), 424u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 3u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 3u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 3u), 412u); CHECK_U(mesh_pipe_ring_cell(&r, 3u), 440u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 4u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 4u), 400u); CHECK_U(mesh_pipe_ring_cell(&r, 4u), 416u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 5u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 5u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 5u), 404u); CHECK_U(mesh_pipe_ring_cell(&r, 5u), 424u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 14u), 6u); CHECK_U(mesh_pipe_ring_full(&r, 14u), 7u); CHECK_U(mesh_pipe_ring_sum(&r, 14u), 408u); CHECK_U(mesh_pipe_ring_cell(&r, 14u), 432u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 2147483647u), 1073741822u); CHECK_U(mesh_pipe_ring_full(&r, 2147483647u), 1073741823u); CHECK_U(mesh_pipe_ring_sum(&r, 2147483647u), 412u); CHECK_U(mesh_pipe_ring_cell(&r, 2147483647u), 440u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967294u), 2147483646u); CHECK_U(mesh_pipe_ring_full(&r, 4294967294u), 2147483647u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967294u), 408u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967294u), 432u); }
    { mesh_pipe_ring r = {0x100, 4, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967295u), 2147483646u); CHECK_U(mesh_pipe_ring_full(&r, 4294967295u), 2147483647u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967295u), 412u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967295u), 440u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 0u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 0u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 0u), 416u); CHECK_U(mesh_pipe_ring_cell(&r, 0u), 448u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1u), 420u); CHECK_U(mesh_pipe_ring_cell(&r, 1u), 456u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 7u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 7u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 7u), 444u); CHECK_U(mesh_pipe_ring_cell(&r, 7u), 504u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 8u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 8u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 8u), 416u); CHECK_U(mesh_pipe_ring_cell(&r, 8u), 448u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 9u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 9u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 9u), 420u); CHECK_U(mesh_pipe_ring_cell(&r, 9u), 456u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 26u), 6u); CHECK_U(mesh_pipe_ring_full(&r, 26u), 7u); CHECK_U(mesh_pipe_ring_sum(&r, 26u), 424u); CHECK_U(mesh_pipe_ring_cell(&r, 26u), 464u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 2147483647u), 536870910u); CHECK_U(mesh_pipe_ring_full(&r, 2147483647u), 536870911u); CHECK_U(mesh_pipe_ring_sum(&r, 2147483647u), 444u); CHECK_U(mesh_pipe_ring_cell(&r, 2147483647u), 504u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967294u), 1073741822u); CHECK_U(mesh_pipe_ring_full(&r, 4294967294u), 1073741823u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967294u), 440u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967294u), 496u); }
    { mesh_pipe_ring r = {0x100, 8, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967295u), 1073741822u); CHECK_U(mesh_pipe_ring_full(&r, 4294967295u), 1073741823u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967295u), 444u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967295u), 504u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 0u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 0u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 0u), 4480u); CHECK_U(mesh_pipe_ring_cell(&r, 0u), 8576u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1u), 4484u); CHECK_U(mesh_pipe_ring_cell(&r, 1u), 8584u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1023u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1023u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1023u), 8572u); CHECK_U(mesh_pipe_ring_cell(&r, 1023u), 16760u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1024u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 1024u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 1024u), 4480u); CHECK_U(mesh_pipe_ring_cell(&r, 1024u), 8576u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1025u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 1025u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 1025u), 4484u); CHECK_U(mesh_pipe_ring_cell(&r, 1025u), 8584u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 3074u), 6u); CHECK_U(mesh_pipe_ring_full(&r, 3074u), 7u); CHECK_U(mesh_pipe_ring_sum(&r, 3074u), 4488u); CHECK_U(mesh_pipe_ring_cell(&r, 3074u), 8592u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 2147483647u), 4194302u); CHECK_U(mesh_pipe_ring_full(&r, 2147483647u), 4194303u); CHECK_U(mesh_pipe_ring_sum(&r, 2147483647u), 8572u); CHECK_U(mesh_pipe_ring_cell(&r, 2147483647u), 16760u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967294u), 8388606u); CHECK_U(mesh_pipe_ring_full(&r, 4294967294u), 8388607u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967294u), 8568u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967294u), 16752u); }
    { mesh_pipe_ring r = {0x100, 1024, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967295u), 8388606u); CHECK_U(mesh_pipe_ring_full(&r, 4294967295u), 8388607u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967295u), 8572u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967295u), 16760u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 0u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 0u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 0u), 262528u); CHECK_U(mesh_pipe_ring_cell(&r, 0u), 524672u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 1u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 1u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 1u), 262532u); CHECK_U(mesh_pipe_ring_cell(&r, 1u), 524680u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 65535u), 0u); CHECK_U(mesh_pipe_ring_full(&r, 65535u), 1u); CHECK_U(mesh_pipe_ring_sum(&r, 65535u), 524668u); CHECK_U(mesh_pipe_ring_cell(&r, 65535u), 1048952u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 65536u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 65536u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 65536u), 262528u); CHECK_U(mesh_pipe_ring_cell(&r, 65536u), 524672u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 65537u), 2u); CHECK_U(mesh_pipe_ring_full(&r, 65537u), 3u); CHECK_U(mesh_pipe_ring_sum(&r, 65537u), 262532u); CHECK_U(mesh_pipe_ring_cell(&r, 65537u), 524680u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 196610u), 6u); CHECK_U(mesh_pipe_ring_full(&r, 196610u), 7u); CHECK_U(mesh_pipe_ring_sum(&r, 196610u), 262536u); CHECK_U(mesh_pipe_ring_cell(&r, 196610u), 524688u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 2147483647u), 65534u); CHECK_U(mesh_pipe_ring_full(&r, 2147483647u), 65535u); CHECK_U(mesh_pipe_ring_sum(&r, 2147483647u), 524668u); CHECK_U(mesh_pipe_ring_cell(&r, 2147483647u), 1048952u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967294u), 131070u); CHECK_U(mesh_pipe_ring_full(&r, 4294967294u), 131071u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967294u), 524664u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967294u), 1048944u); }
    { mesh_pipe_ring r = {0x100, 65536, 8}; CHECK_U(mesh_pipe_ring_empty(&r, 4294967295u), 131070u); CHECK_U(mesh_pipe_ring_full(&r, 4294967295u), 131071u); CHECK_U(mesh_pipe_ring_sum(&r, 4294967295u), 524668u); CHECK_U(mesh_pipe_ring_cell(&r, 4294967295u), 1048952u); }
    return mesh_check_done();
}
