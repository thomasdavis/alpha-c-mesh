/* jobs.rs's `select_is_the_schedulers_filter_and_score`: select with the fleet scheduler's model. */
#include "mesh/jobs/sched.h"
#include "mesh/check.h"
#include "mesh/strbuf.h"
#include "mesh/strmap.h"

#include <stdlib.h>
#include <string.h>

/* The scheduler's model of n CUDA members running "p". */
static mjs_cx cx(uint32_t n) {
    mjs_cx c = {0};
    c.members = mxalloc(n * sizeof *c.members);
    c.nmembers = n;
    for (uint32_t k = 0; k < n; k++) {
        msm_member m = msm_member_default();
        m.id = k;
        m.up = true;
        MSM_SET(m.gpu, "cuda");
        MSM_SET(m.arch, "aarch64");
        m.cpus = 8;
        m.ram_mb = 1000;
        m.mem_avail_mb = 800;
        m.gpu_share = mof32_some(0.9f);
        m.load_known = true;
        c.members[k] = m;
    }
    c.need_names = mxalloc(sizeof *c.need_names);
    c.need_names[0] = mstrdup("p");
    c.needs = mxalloc(sizeof *c.needs);
    c.needs[0] = msn_default();
    c.nneeds = 1;
    c.w = msw_default();
    return c;
}

static mj_member_progs progs4(void) {
    mj_member_progs p = {0};
    for (uint32_t k = 0; k < 4; k++) {
        mj_prog *x = mxalloc(sizeof *x);
        *x = (mj_prog){mstrdup(k == 3 ? "q" : "p"), 0, 8};
        mj_member_progs_set(&p, k, x, 1);
    }
    return p;
}

static void oks(const mj_picks *v, const char *want) {
    char got[64] = "";
    for (size_t i = 0; i < v->n; i++)
        if (v->v[i].ok) {
            char b[8];
            snprintf(b, sizeof b, "%s%u", got[0] ? "," : "", v->v[i].member);
            strcat(got, b);
        }
    CHECK_S(got, want);
}

static const char *why(const mj_picks *v, uint32_t k) {
    for (size_t i = 0; i < v->n; i++)
        if (v->v[i].member == k) return v->v[i].why ? v->v[i].why : "";
    return "";
}

static mj_spec spec(uint32_t members) {
    mj_setup su = {6, 0};
    return (mj_spec){.id = 1, .program = mstrdup("p"), .op = 254, .items = mj_ranges_of(0, 100), .attempts = 3, .setup = mj_setup_clone(&su, 1),
                     .nsetup = 1, .submitted_ms = 5, .members = members};
}

static void perf(mjs_cx *c, uint32_t k, float task_us) {
    msm_perf p = {0};
    strcpy(p.exec, "cuda");
    p.run = true;
    p.task_us = mof32_some(task_us);
    msm_perfs_set(&c->perf, "p", k, &p);
}

int main(void) {
    mj_member_progs progs = progs4();
    mj_spec job = spec(0);
    /* No fleet (a single meshd): alive and running it. */
    mj_picks v = mj_select(&job, 0xf, &progs, NULL);
    oks(&v, "0,1,2");
    mj_picks_free(&v);
    /* Cordoned, busy (an idle member runs it), not running it, down. */
    mjs_cx c = cx(5);
    c.members[1].cordoned = true;
    c.members[2].busy = mstrdup("cpu psi 0.40");
    mj_sched *s = mjs_sched(c);
    v = mj_select(&job, 0x0f, &progs, s);
    oks(&v, "0");
    CHECK_S(why(&v, 1), "cordoned");
    CHECK(strncmp(why(&v, 2), "busy (owner policy: cpu psi 0.40)", 33) == 0);
    CHECK(strstr(why(&v, 3), "does not run") != NULL);
    CHECK_S(why(&v, 4), "down");
    char *l = mj_pick_line(&v.v[0]);
    CHECK(strncmp(l, "pick system 0 eligible ", 23) == 0);
    free(l);
    mj_picks_free(&v);
    /* The busy member alone: eligible, floor-sized leases. */
    ((mjs_cx *)s->ctx)->members[0].cordoned = true;
    v = mj_select(&job, 0xf, &progs, s);
    oks(&v, "2");
    l = mj_pick_line(&v.v[0]);
    CHECK(v.v[0].busy && strstr(l, "floor-sized"));
    free(l);
    mj_picks_free(&v);
    s->release(s->ctx);
    free(s);
    /* Backends, a failure there, the job's members. */
    c = cx(3);
    MSM_SET(c.members[1].gpu, "metal");
    ms_set(&c.members[2].fails, "p", "trap", true);
    s = mjs_sched(c);
    v = mj_select(&job, 0x7, &progs, s);
    oks(&v, "0");
    CHECK(strncmp(why(&v, 1), "backends cuda", 13) == 0);
    CHECK(strstr(why(&v, 2), "failed there: trap") != NULL);
    mj_picks_free(&v);
    s->release(s->ctx);
    free(s);
    mj_spec j2 = spec(0x2);
    s = mjs_sched(cx(3));
    v = mj_select(&j2, 0x7, &progs, s);
    oks(&v, "1");
    mj_picks_free(&v);
    s->release(s->ctx);
    free(s);
    mj_spec_free(&j2);
    /* Measured performance and share order them: the faster member first, ties to the lower id. */
    c = cx(3);
    perf(&c, 0, 400.0f);
    perf(&c, 1, 200.0f);
    perf(&c, 2, 100.0f);
    s = mjs_sched(c);
    v = mj_select(&job, 0x7, &progs, s);
    oks(&v, "2,1,0");
    mj_picks_free(&v);
    ((mjs_cx *)s->ctx)->members[2].gpu_share = mof32_some(0.1f);
    v = mj_select(&job, 0x7, &progs, s);
    CHECK(v.v[0].ok && v.v[0].member == 1);
    mj_picks_free(&v);
    s->release(s->ctx);
    free(s);
    mj_spec_free(&job);
    mj_member_progs_free(&progs);
    return mesh_check_done();
}
