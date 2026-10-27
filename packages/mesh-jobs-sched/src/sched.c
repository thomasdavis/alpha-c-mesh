/* jobs.rs select's scheduler half: the verdict and score of an alive member that runs the program. */
#include "mesh/jobs/sched.h"
#include "mesh/sched_score.h"
#include "mesh/strbuf.h"
#include "mesh/strmap.h"

#include <stdlib.h>
#include <string.h>

void mjs_cx_free(mjs_cx *cx) {
    for (size_t i = 0; i < cx->nmembers; i++) msm_member_free(&cx->members[i]);
    free(cx->members);
    for (size_t i = 0; i < cx->nneeds; i++) {
        free(cx->need_names[i]);
        mesh_needs_free(&cx->needs[i]);
    }
    free(cx->need_names);
    free(cx->needs);
    for (size_t i = 0; i < cx->perf.n; i++) free(cx->perf.name[i]);
    free(cx->perf.name);
    free(cx->perf.member);
    free(cx->perf.perf);
    memset(cx, 0, sizeof *cx);
}

const mesh_needs *mjs_needs(const mjs_cx *cx, const char *name) {
    for (size_t i = 0; i < cx->nneeds; i++)
        if (strcmp(cx->need_names[i], name) == 0) return &cx->needs[i];
    return NULL;
}

/* The best measured time per task of prog on any member that runs it. */
static mof32 best_task(const mjs_cx *cx, const char *prog) {
    mof32 best = mof32_none();
    for (size_t i = 0; i < cx->perf.n; i++) {
        const msm_perf *p = &cx->perf.perf[i];
        if (strcmp(cx->perf.name[i], prog) != 0 || !p->run || !p->task_us.some || !(p->task_us.v > 0.0f)) continue;
        if (!best.some || p->task_us.v < best.v) best = mof32_some(p->task_us.v);
    }
    return best;
}

static int judge(void *ctx, uint32_t k, const char *prog, mj_score *score, bool *busy, char **why) {
    const mjs_cx *cx = ctx;
    const msm_member *m = &cx->members[k];
    const mesh_needs *nd = mjs_needs(cx, prog);
    if (!nd) return -1;
    char exec[16], *err = NULL;
    if (!msc_filter(m, nd, exec, &err)) {
        *why = err;
        return 0;
    }
    const char *failed = ms_get(&m->fails, prog);
    if (failed) {
        *why = mfmt("%s failed there: %s", prog, failed);
        return 0;
    }
    const msm_perf *here = msm_perfs_get(&cx->perf, prog, k);
    *busy = m->busy != NULL;
    msc_cx scx = {.w = &cx->w, .current = true, .here = here, .best_task_us = best_task(cx, prog), .own_cpu = here ? here->cpu : 0.0f,
                  .placed = 0, .max_placed = 0, .rtt_ms = mof32_none()};
    msc_score_t s = msc_score_terms(m, nd, &scx);
    score->total = s.total;
    score->explain = msc_explain(&s);
    return 1;
}

static char *busy_why(void *ctx, uint32_t k) {
    const mjs_cx *cx = ctx;
    if (k >= cx->nmembers || !cx->members[k].busy) return NULL;
    return mstrdup(cx->members[k].busy);
}

static void release(void *ctx) {
    mjs_cx_free(ctx);
    free(ctx);
}

mj_sched *mjs_sched(mjs_cx cx) {
    mjs_cx *c = mxalloc(sizeof *c);
    *c = cx;
    mj_sched *s = mxalloc(sizeof *s);
    *s = (mj_sched){c, (uint32_t)cx.nmembers, judge, busy_why, release};
    return s;
}
