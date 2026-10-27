/* @ajax/pathtrace-default-scene: programs/pathtrace's default scene, as the scene format's u32 words
 * (@ajax/pathtrace-scene): 640x360, 8 spp x 32 passes, depth 8; 9 materials, 6 quads, 4 spheres and 20
 * triangles. Freestanding. */
#ifndef PATHTRACE_DEFAULT_SCENE_H
#define PATHTRACE_DEFAULT_SCENE_H

#include <stdint.h>

#define PT_DEFAULT_SCENE_WORDS 552
extern const uint32_t pt_default_scene[PT_DEFAULT_SCENE_WORDS];

#endif
