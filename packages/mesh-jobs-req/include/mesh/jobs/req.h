/* @ajax/mesh-jobs-req: a change of the job table as the control port's words (jobs.rs `Submit`,
 * `Req`, `SUBMIT_USAGE`): `job submit <program> items <..> [op ..] [priority ..] [attempts ..]
 * [deadline <secs>] [results collect|nonzero|none] [setup <op>:<arg>,..] [members <k>,..]`,
 * `job cancel <id>`, `job forget <id>`, `job tone <id> aces|reinhard` (the words after `job`). */
#ifndef MESH_JOBS_REQ_H
#define MESH_JOBS_REQ_H

#include "mesh/jobs/id.h"
#include "mesh/jobs/ranges.h"
#include "mesh/jobs/spec.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MESH_JOBS_SUBMIT_USAGE                                                                                                   \
    "usage: job submit <program> items <n | a-b | a- | a,b-c,..> [op <o>] [priority <p>] [attempts <a>] [deadline <secs>] " \
    "[results collect|nonzero|none] [setup <op>:<arg>,..] [members <k>,..]"

typedef struct {
    uint32_t members;
    char *program;
    uint32_t op;
    mj_ranges items;
    int32_t priority;
    uint32_t attempts;
    uint64_t deadline_s;
    mj_mode mode;
    mj_setup *setup;
    size_t nsetup;
} mj_submit;

typedef enum { MJ_REQ_SUBMIT, MJ_REQ_CANCEL, MJ_REQ_FORGET, MJ_REQ_TONE } mj_req_kind;

typedef struct {
    mj_req_kind kind;
    mj_submit submit; /* SUBMIT */
    mj_job_id job;    /* CANCEL, FORGET, TONE */
    uint32_t tone;    /* TONE: 0 ACES, 1 Reinhard */
} mj_req;

void mj_submit_free(mj_submit *s);
mj_submit mj_submit_clone(const mj_submit *s);
void mj_req_free(mj_req *r);
mj_req mj_req_clone(const mj_req *r);

/* The words after `submit`. False with *err (the Rust's message; the caller frees it). */
bool mj_submit_parse(const char *const *words, size_t n, mj_submit *out, char **err);
/* The words after `job`. */
bool mj_req_parse(const char *const *words, size_t n, mj_req *out, char **err);

#endif
