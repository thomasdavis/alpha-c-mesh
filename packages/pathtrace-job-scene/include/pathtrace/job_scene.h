/* @ajax/pathtrace-job-scene: a fleet job's scene (pathtrace-core lib.rs `job_scene`): the default
 * scene with the job's setup tasks applied in order, as every member's program applies them to its
 * view. Freestanding. */
#ifndef PATHTRACE_JOB_SCENE_H
#define PATHTRACE_JOB_SCENE_H

#include "pathtrace/default_scene.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t op, arg;
} pt_setup_task;

/* Writes the scene into out (PT_DEFAULT_SCENE_WORDS words) and returns -1; or, when setup task i is
 * neither OP_DEFAULT nor a valid OP_SET, returns i (out then holds the scene so far). Validate the
 * result with pt_validate_in(.., PT_FLEET_MAX_TILES). */
long pt_job_scene(const pt_setup_task *setup, size_t n, uint32_t out[PT_DEFAULT_SCENE_WORDS]);

#endif
