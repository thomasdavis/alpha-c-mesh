/* @ajax/mesh-abi-host-links: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/host_links.h"

const char *const MESH_KIND_NAMES[MESH_NET_KINDS] = {"round", "place", "state", "ctl"};

const mesh_field MESH_LINK_FIELDS[6] = {
    {MESH_L_RX_BYTES, 8},
    {MESH_L_TX_BYTES, 8},
    {MESH_L_RX_FRAMES, 8 * MESH_NET_KINDS},
    {MESH_L_TX_FRAMES, 8 * MESH_NET_KINDS},
    {MESH_L_LOCAL, 32},
    {MESH_L_IFACE, 16},
};

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_S_LINKS % 8 == 0), "S_LINKS.is_multiple_of(8)");
_Static_assert((MESH_LINK_BYTES % 8 == 0), "LINK_BYTES.is_multiple_of(8)");
_Static_assert(MESH_L_RX_FRAMES + 8 * MESH_NET_KINDS == MESH_L_TX_FRAMES, "L_RX_FRAMES + 8 * NET_KINDS == L_TX_FRAMES");
_Static_assert(MESH_L_TX_FRAMES + 8 * MESH_NET_KINDS <= MESH_L_LOCAL, "L_TX_FRAMES + 8 * NET_KINDS <= L_LOCAL");
