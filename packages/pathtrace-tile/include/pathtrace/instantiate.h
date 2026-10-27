/* Instantiates the whole renderer of programs/pathtrace for one float type (see pathtrace/real.h):
 * define PT_SUFFIX and the operation macros, then include this. Every function is named
 * pt_<name><PT_SUFFIX> and has PT_API linkage (default static inline). The macros are left defined;
 * #undef them before instantiating again in the same translation unit. */
#include "pathtrace/t_math.h"
#include "pathtrace/t_intersect.h"
#include "pathtrace/t_sample.h"
#include "pathtrace/t_tonemap.h"
#include "pathtrace/t_tile.h"
