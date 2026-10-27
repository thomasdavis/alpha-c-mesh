/* @ajax/mesh-abi-host-links: its tables and functions. */
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
