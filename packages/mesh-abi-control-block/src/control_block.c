/* @ajax/mesh-abi-control-block: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/control_block.h"

const mesh_field MESH_CTL_FIELDS[17] = {
    {MESH_P_SHUTDOWN, 4},
    {MESH_P_ONLINE, 4},
    {MESH_P_HALTED, 4},
    {MESH_P_RUN, 4},
    {MESH_P_RUN_REQ, 4},
    {MESH_P_FAULT, 4},
    {MESH_P_RING_TAIL, 8},
    {MESH_P_REQ_SEQ, 8},
    {MESH_P_RESP_SEQ, 8},
    {MESH_P_REQ_LEN, 4},
    {MESH_P_RESP_LEN, 4},
    {MESH_P_LAUNCHES, 4},
    {MESH_P_PID, 4},
    {MESH_P_SPAWNS, 4},
    {MESH_P_CRASHES, 4},
    {MESH_P_EXIT, 4},
    {MESH_P_LIMITS, 4},
};

/* The Rust const asserts that only involve this package. */
_Static_assert(MESH_P_LAUNCHES + 4 <= MESH_LINE, "P_LAUNCHES + 4 <= LINE");
_Static_assert((MESH_P_RING_TAIL % 8 == 0), "P_RING_TAIL.is_multiple_of(8)");
_Static_assert((MESH_P_REQ_SEQ % 8 == 0), "P_REQ_SEQ.is_multiple_of(8)");
_Static_assert(MESH_P_RING_HEAD == MESH_LINE, "P_RING_HEAD == LINE");
_Static_assert(MESH_P_DONE == 2 * MESH_LINE, "P_DONE == 2 * LINE");
_Static_assert(MESH_P_COUNTERS == 3 * MESH_LINE, "P_COUNTERS == 3 * LINE");
