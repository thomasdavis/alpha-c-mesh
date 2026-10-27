/* @ajax/mesh-abi-native: compile-time layout checks (the header holds everything else). */
#include "mesh/abi/native.h"

/* Nothing to compile: a header-only package. sexc needs a source until it builds
 * header-only libraries; this keeps the header compiled and warning-free. */
_Static_assert(sizeof(int) >= 4, "the mesh contract assumes 32-bit int or wider");
