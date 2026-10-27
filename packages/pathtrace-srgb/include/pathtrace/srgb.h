/* @ajax/pathtrace-srgb: the sRGB quantization table of programs/pathtrace: pt_srgb_t[k] is the least
 * linear value (as f32 bits) whose 8-bit sRGB code is k; pt_srgb_t[0] is 0. Freestanding. */
#ifndef PATHTRACE_SRGB_H
#define PATHTRACE_SRGB_H

#include <stdint.h>

extern const uint32_t pt_srgb_t[256];

#endif
