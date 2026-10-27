/* @ajax/mesh-abi-control-protocol: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/control_protocol.h"

const char *const MESH_LOAD_REASONS[13] = {"manifest", "abi", "name exists", "no slot", "view heap", "occupancy", "regs", "jit", "load", "peer", "unsupported", "float", "exec"};
