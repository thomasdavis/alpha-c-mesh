/* jobs.rs's select and Pick. */
#include "mesh/jobs/select.h"

#include <stdlib.h>
#include <string.h>

void mj_picks_free(mj_picks *p) {
    for (size_t i = 0; i < p->n; i++) {
        free(p->v[i].score.explain);
        free(p->v[i].why);
    }
    free(p->v);
    *p = (mj_picks){0};
}

mj_picks mj_picks_clone(const mj_picks *p) {
    mj_picks c = {p->n ? mxalloc(p->n * sizeof *c.v) : NULL, p->n};
    for (size_t i = 0; i < p->n; i++) {
        c.v[i] = p->v[i];
        c.v[i].score.explain = p->v[i].score.explain ? mstrdup(p->v[i].score.explain) : NULL;
        c.v[i].why = p->v[i].why ? mstrdup(p->v[i].why) : NULL;
    }
    return c;
}

char *mj_pick_line(const mj_pick *p) {
    if (p->ok)
        return mfmt("pick system %u eligible %s%s", p->member, p->score.explain ? p->score.explain : "",
                    p->busy ? " busy: floor-sized leases (the only member)" : "");
    return mfmt("pick system %u rejected: %s", p->member, p->why ? p->why : "");
}

const mj_pick *mj_picks_ok(const mj_picks *p, uint32_t m) {
    for (size_t i = 0; i < p->n; i++)
        if (p->v[i].member == m && p->v[i].ok) return &p->v[i];
    return NULL;
}

void mj_member_progs_free(mj_member_progs *p) {
    for (uint32_t k = 0; k < MJ_MEMBERS; k++) mj_progs_free(p->v[k], p->n[k]);
    memset(p, 0, sizeof *p);
}

void mj_member_progs_set(mj_member_progs *p, uint32_t k, mj_prog *progs, size_t n) {
    mj_progs_free(p->v[k], p->n[k]);
    p->has[k] = true;
    p->v[k] = progs;
    p->n[k] = n;
}

void mj_member_progs_remove(mj_member_progs *p, uint32_t k) {
    mj_progs_free(p->v[k], p->n[k]);
    p->has[k] = false;
    p->v[k] = NULL;
    p->n[k] = 0;
}

/* The job's first program member k runs (a multi-program job: any of them decides). */
static const char *runs_which(const mj_spec *job, const mj_member_progs *progs, uint32_t k, size_t *len) {
    if (k >= MJ_MEMBERS || !progs->has[k]) return NULL;
    const char *p = job->program;
    for (;;) {
        const char *c = strchr(p, ',');
        size_t n = c ? (size_t)(c - p) : strlen(p);
        for (size_t i = 0; i < progs->n[k]; i++)
            if (strlen(progs->v[k][i].name) == n && memcmp(progs->v[k][i].name, p, n) == 0) {
                *len = n;
                return p;
            }
        if (!c) return NULL;
        p = c + 1;
    }
}

static int rank_cmp(const mj_pick *a, const mj_pick *b) {
    int ka = a->ok ? 0 : 1, kb = b->ok ? 0 : 1;
    if (ka != kb) return ka < kb ? -1 : 1;
    double ta = a->ok ? -a->score.total : 0.0, tb = b->ok ? -b->score.total : 0.0;
    if (ta != tb) return ta < tb ? -1 : 1; /* the totals are never NaN */
    return a->member < b->member ? -1 : a->member > b->member ? 1 : 0;
}

mj_picks mj_select(const mj_spec *job, uint32_t alive, const mj_member_progs *progs, const mj_sched *cx) {
    uint32_t allowed = job->members == 0 ? UINT32_MAX : job->members;
    uint32_t n = cx ? cx->nmembers : 0;
    for (uint32_t k = 0; k < MJ_MEMBERS; k++)
        if (progs->has[k] && k + 1 > n) n = k + 1;
    mj_picks out = {n ? mxalloc(n * sizeof *out.v) : NULL, n};
    for (uint32_t k = 0; k < n; k++) {
        mj_pick *p = &out.v[k];
        p->member = k;
        size_t len = 0;
        const char *which = runs_which(job, progs, k, &len);
        if ((alive >> k & 1) == 0) p->why = mstrdup("down");
        else if ((allowed >> k & 1) == 0) p->why = mstrdup("not in the job's members");
        else if (!which) p->why = mfmt("%s does not run there (not placed there, or not started)", job->program);
        else if (!cx || k >= cx->nmembers) {
            p->ok = true;
            p->score.explain = mstrdup(MJ_SCORE_DEFAULT_EXPLAIN);
        } else {
            char *prog = mstrndup(which, len);
            bool busy = false;
            int v = cx->judge(cx->ctx, k, prog, &p->score, &busy, &p->why);
            free(prog);
            if (v < 0) {
                p->ok = true;
                p->score = (mj_score){0.0, mstrdup(MJ_SCORE_DEFAULT_EXPLAIN)};
            } else if (v > 0) {
                p->ok = true;
                p->busy = busy;
            }
        }
    }
    /* HTCondor's backoff: a busy member gets leases only when no idle member runs the program. */
    bool idle = false;
    for (uint32_t k = 0; k < n; k++)
        if (out.v[k].ok && !out.v[k].busy) idle = true;
    if (idle)
        for (uint32_t k = 0; k < n; k++) {
            mj_pick *p = &out.v[k];
            if (!p->ok || !p->busy) continue;
            char *why = cx && cx->busy_why ? cx->busy_why(cx->ctx, p->member) : NULL;
            free(p->score.explain);
            p->score = (mj_score){0};
            p->ok = false;
            p->why = mfmt("busy (owner policy: %s): leases go to the idle members", why ? why : "");
            p->busy = false;
            free(why);
        }
    /* A stable sort: eligible first by score, then the rest, each by member id. */
    for (uint32_t i = 1; i < n; i++) {
        mj_pick x = out.v[i];
        uint32_t j = i;
        while (j > 0 && rank_cmp(&out.v[j - 1], &x) > 0) {
            out.v[j] = out.v[j - 1];
            j--;
        }
        out.v[j] = x;
    }
    return out;
}
