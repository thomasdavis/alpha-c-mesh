/* @ajax/mesh-abi-backend: its tables and functions, and its compile-time layout checks. */
#include "mesh/abi/backend.h"

const char *const MESH_BACKENDS[4] = {"cuda", "metal", "wasm", "native"};
