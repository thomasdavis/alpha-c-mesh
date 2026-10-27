/* @ajax/mesh-jobs-spec: what a job is (jobs.rs `JobSpec`, `Mode`): a program (or several joined
 * by commas), an op, its items, a priority, a retry policy, a deadline, a result mode, setup tasks,
 * when it was submitted and which members may run it; and its one-line text form (`encode`,
 * `decode`), which is how a Submit op carries it. */
#ifndef MESH_JOBS_SPEC_H
#define MESH_JOBS_SPEC_H

#include "mesh/jobs/id.h"
#include "mesh/jobs/ranges.h"
#include "mesh/strbuf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* collect: every item's result and bytes; nonzero: only the items whose result isn't 0; none: only
 * which items are done. */
typedef enum { MJ_MODE_COLLECT, MJ_MODE_NONZERO, MJ_MODE_NONE } mj_mode;

const char *mj_mode_word(mj_mode m);
bool mj_mode_parse(const char *s, mj_mode *out);

typedef struct {
    uint32_t op, arg;
} mj_setup;

typedef struct {
    mj_job_id id;
    char *program;
    uint32_t op;
    mj_ranges items;
    int32_t priority;      /* higher first; ties: the older job */
    uint32_t attempts;     /* tries an item gets before it is recorded failed (>= 1) */
    uint64_t deadline_ms;  /* unix ms after which what is left is given up (0: none) */
    mj_mode mode;
    mj_setup *setup;       /* (op, arg) tasks a member runs before its first item of this job */
    size_t nsetup;
    uint64_t submitted_ms;
    uint32_t members;      /* bit k: may run on member k (0: every member that runs the program) */
} mj_spec;

void mj_spec_free(mj_spec *s);
mj_spec mj_spec_clone(const mj_spec *s);
bool mj_spec_eq(const mj_spec *a, const mj_spec *b);
mj_setup *mj_setup_clone(const mj_setup *s, size_t n);

/* Its items never end (the last range is endless). */
bool mj_spec_endless(const mj_spec *s);
/* Whether `name` is one of its programs. */
bool mj_spec_runs(const mj_spec *s, const char *name);
/* Its first program (up to the first comma): a pointer into s->program and a length. */
size_t mj_spec_first_program(const mj_spec *s, const char **p);

/* `id 4294967297 program p op 254 items 0-10 priority 0 attempts 3 deadline 0 mode collect setup 6:0
 * submitted 5 members 0x5` */
void mj_spec_encode(const mj_spec *s, msb *out);
/* The encoded form back; false with *err (the caller frees it) when it isn't one. */
bool mj_spec_decode(const char *t, mj_spec *out, char **err);

#endif
