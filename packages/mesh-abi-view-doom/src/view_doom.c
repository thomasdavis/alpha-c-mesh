/* @ajax/mesh-abi-view-doom: its tables and functions. */
#include "mesh/abi/view_doom.h"

const mesh_doom_region MESH_DOOM_PLACE_STATE[3] = {{0x0, 0x24}, {MESH_DOOM_D_KEYS, 4}, {MESH_DOOM_D_MAP, 0x400}};

const mesh_doom_region MESH_DOOM_PLACE_OUT[4] = {{MESH_DOOM_D_HEIGHT, 4 * MESH_DOOM_WIDTH}, {MESH_DOOM_D_KIND, 4 * MESH_DOOM_WIDTH}, {MESH_DOOM_D_SIDE, 4 * MESH_DOOM_WIDTH}, {MESH_DOOM_D_DIST, 4 * MESH_DOOM_WIDTH}};

const float MESH_DOOM_START[6] = {8.5f, 10.5f, 0.0f, -1.0f, MESH_DOOM_PLANE, 0.0f};

const char MESH_DOOM_MAP[16][17] = {
    "1111111111111111",
    "1......2.......1",
    "1......2..3.3..1",
    "1..44..2.......1",
    "1..44.....3.3..1",
    "1......2.......1",
    "12222.2222..2221",
    "1..............1",
    "1.5.5.....6....1",
    "1........666...1",
    "1.5.5.....6....1",
    "1..............1",
    "1777.7777..7..71",
    "1...........7..1",
    "1...7...7......1",
    "1111111111111111",
};
