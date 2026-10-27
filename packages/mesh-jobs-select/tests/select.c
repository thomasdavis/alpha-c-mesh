/* select without a fleet (a single meshd), and with a scheduler stub: verdicts, the owner policy's
 * backoff, the order. (With the fleet's own scheduler: @ajax/mesh-jobs-sched's test, the Rust's
 * `select_is_the_schedulers_filter_and_score`.) */
#include "mesh/jobs/select.h"
#include "mesh/check.h"

#include <stdlib.h>
#include <string.h>

static mj_prog *one(const char *name) {
    mj_prog *p = mxalloc(sizeof *p);
    *p = (mj_prog){mstrdup(name), 0, 8};
    return p;
}

static mj_spec job(uint32_t members) {
    mj_spec s = {.id = 1, .program = mstrdup("p"), .op = 254, .items = mj_ranges_of(0, 100), .attempts = 3, .members = members};
    return s;
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

/* A stub scheduler: member 1 cordoned, member 2 busy, scores by member (0 lowest). */
static double score_of[4] = {1.0, 2.0, 3.0, 0.5};
static int judge(void *ctx, uint32_t k, const char *prog, mj_score *s, bool *busy, char **w) {
    (void)ctx;
    (void)prog;
    if (k == 1) {
        *w = mstrdup("cordoned");
        return 0;
    }
    if (k == 3) return -1;
    *s = (mj_score){score_of[k], mfmt("%.3f = stub", score_of[k])};
    *busy = k == 2;
    return 1;
}
static char *busy_why(void *ctx, uint32_t k) {
    (void)ctx;
    return k == 2 ? mstrdup("cpu psi 0.40") : NULL;
}

int main(void) {
    mj_member_progs progs = {0};
    for (uint32_t k = 0; k < 4; k++) mj_member_progs_set(&progs, k, one(k == 3 ? "q" : "p"), 1);
    mj_spec j = job(0);
    /* No fleet (a single meshd): alive and running it. */
    mj_picks v = mj_select(&j, 0xf, &progs, NULL);
    oks(&v, "0,1,2");
    CHECK(strstr(why(&v, 3), "does not run there") != NULL);
    char *l = mj_pick_line(&v.v[0]);
    CHECK_S(l, "pick system 0 eligible " MJ_SCORE_DEFAULT_EXPLAIN);
    free(l);
    mj_picks_free(&v);
    v = mj_select(&j, 0xb, &progs, NULL);
    CHECK_S(why(&v, 2), "down");
    mj_picks_free(&v);
    mj_spec_free(&j);
    j = job(0x2);
    v = mj_select(&j, 0xf, &progs, NULL);
    oks(&v, "1");
    CHECK_S(why(&v, 0), "not in the job's members");
    mj_picks_free(&v);
    mj_spec_free(&j);
    /* With a scheduler: cordoned rejected, busy backed off while 0 is idle, best first. */
    mj_sched cx = {NULL, 4, judge, busy_why, NULL};
    mj_member_progs_set(&progs, 3, one("p"), 1);
    j = job(0);
    v = mj_select(&j, 0xf, &progs, &cx);
    oks(&v, "0,3");
    CHECK_S(why(&v, 1), "cordoned");
    CHECK_S(why(&v, 2), "busy (owner policy: cpu psi 0.40): leases go to the idle members");
    CHECK(v.v[0].member == 0 && v.v[1].member == 3); /* 1.0 then the unscored 0.0 */
    l = mj_pick_line(&v.v[2]);
    CHECK_S(l, "pick system 1 rejected: cordoned");
    free(l);
    mj_picks c = mj_picks_clone(&v);
    CHECK(mj_picks_ok(&c, 3) != NULL && mj_picks_ok(&c, 2) == NULL);
    mj_picks_free(&c);
    mj_picks_free(&v);
    /* The busy member alone (the others down): eligible, floor-sized leases. */
    v = mj_select(&j, 0x4, &progs, &cx);
    oks(&v, "2");
    CHECK(v.v[0].busy);
    l = mj_pick_line(&v.v[0]);
    CHECK_S(l, "pick system 2 eligible 3.000 = stub busy: floor-sized leases (the only member)");
    free(l);
    mj_picks_free(&v);
    mj_spec_free(&j);
    mj_member_progs_free(&progs);
    return mesh_check_done();
}
