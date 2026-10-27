/* @ajax/mesh-abi-host-net: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/host_net.h"

/* The Rust const asserts that only involve this package. */
_Static_assert((MESH_S_NET_AT_MS % 8 == 0), "S_NET_AT_MS.is_multiple_of(8)");
_Static_assert((MESH_S_CTL_RX % 8 == 0), "S_CTL_RX.is_multiple_of(8)");
_Static_assert(MESH_S_HTTP_CONNS + 8 <= MESH_S_NET_TOTAL, "S_HTTP_CONNS + 8 <= S_NET_TOTAL");
_Static_assert((MESH_S_NET_TOTAL % 8 == 0), "S_NET_TOTAL.is_multiple_of(8)");
