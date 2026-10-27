/* A fleet job's scene (pathtrace-core lib.rs `job_scene`). */
#include "pathtrace/job_scene.h"

#include "pathtrace/scene.h"
#include "pathtrace/view.h"

static void load_default(uint32_t *out) {
    for (size_t i = 0; i < PT_DEFAULT_SCENE_WORDS; i++) out[i] = pt_default_scene[i];
}

long pt_job_scene(const pt_setup_task *setup, size_t n, uint32_t out[PT_DEFAULT_SCENE_WORDS]) {
    load_default(out);
    for (size_t i = 0; i < n; i++) {
        if (setup[i].op == PT_OP_DEFAULT)
            load_default(out);
        else if (!(setup[i].op == PT_OP_SET && pt_set_param(out, PT_DEFAULT_SCENE_WORDS, setup[i].arg)))
            return (long)i;
    }
    return -1;
}
