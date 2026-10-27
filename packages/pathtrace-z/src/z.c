/* The renderer instantiated in the cluster float mode (mesh-ref pathtrace.rs `impl Real for Z`). */
#include "pathtrace/z.h"

#include "mesh/ftz.h"

#define PT_SUFFIX _z
#define PT_API
#define PT_ADD(a, b) mesh_ftz_add(a, b)
#define PT_SUB(a, b) mesh_ftz_sub(a, b)
#define PT_MUL(a, b) mesh_ftz_mul(a, b)
#define PT_DIV(a, b) mesh_ftz_div(a, b)
#define PT_NEG(a) mesh_ftz_neg(a)
#define PT_LT(a, b) mesh_ftz_lt(a, b)
#define PT_GT(a, b) mesh_ftz_gt(a, b)
#define PT_LE(a, b) mesh_ftz_le(a, b)
#define PT_GE(a, b) mesh_ftz_ge(a, b)
#include "pathtrace/instantiate.h"
