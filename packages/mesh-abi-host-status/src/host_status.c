/* @ajax/mesh-abi-host-status: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/host_status.h"

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_S_LAST_BLINK_MS + 8 <= MESH_S_ISOLATION, "S_LAST_BLINK_MS + 8 <= S_ISOLATION");
_Static_assert(MESH_S_ISOLATION + 4 <= MESH_S_SYSTEMS, "S_ISOLATION + 4 <= S_SYSTEMS");
_Static_assert(MESH_S_ARCH + 16 <= MESH_S_LOCK_WAIT_US, "S_ARCH + 16 <= S_LOCK_WAIT_US");
_Static_assert(MESH_S_LOCK_VIEW + 32 <= MESH_S_LOCK_NAME, "S_LOCK_VIEW + 32 <= S_LOCK_NAME");
_Static_assert(MESH_S_HTTP_ADDR + 64 <= MESH_S_LOCK_VIEW, "S_HTTP_ADDR + 64 <= S_LOCK_VIEW");
_Static_assert((MESH_S_LOCK_STEP % 8 == 0), "S_LOCK_STEP.is_multiple_of(8)");
_Static_assert((MESH_S_LAST_BLINK_MS % 8 == 0), "S_LAST_BLINK_MS.is_multiple_of(8)");
