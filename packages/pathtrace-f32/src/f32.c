/* The renderer instantiated with plain f32 operations (pathtrace-core `impl Real for f32`). */
#include "pathtrace/f32.h"

#define PT_SUFFIX _f32
#define PT_API
#define PT_ADD(a, b) ((a) + (b))
#define PT_SUB(a, b) ((a) - (b))
#define PT_MUL(a, b) ((a) * (b))
#define PT_DIV(a, b) ((a) / (b))
#define PT_NEG(a) (-(a))
#define PT_LT(a, b) ((a) < (b))
#define PT_GT(a, b) ((a) > (b))
#define PT_LE(a, b) ((a) <= (b))
#define PT_GE(a, b) ((a) >= (b))
#include "pathtrace/instantiate.h"
