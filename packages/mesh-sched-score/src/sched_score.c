/* sched.rs: filter, eligible, Score, Cx, score_terms, score.
 *
 * The arithmetic keeps the Rust's types and order exactly (f32 where the Rust computes in f32, then
 * widened), so the C and Rust schedulers decide the same from the same inputs. */
#include "mesh/sched_score.h"
#include "mesh/abi/base.h"
#include "mesh/mt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Rust's f32::clamp / f64::clamp (NaN stays NaN). */
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static double clampd(double x, double lo, double hi) { return x < lo ? lo : x > hi ? hi : x; }

bool msc_filter(const msm_member *m, const mesh_needs *n, char exec[16], char **err) {
    if (!m->up) {
        *err = mt_fmt("down for %.1f s", (double)m->down_ms / 1e3);
        return false;
    }
    if (m->cordoned) {
        *err = mt_strdup("cordoned");
        return false;
    }
    const char *e = msm_exec_for(m, n->backends, n->nbackends);
    if (!e) {
        const char *ex[3];
        size_t k = msm_executors(m, ex);
        mt_strs runs = {0};
        for (size_t i = 0; i < k; i++) mt_push(&runs, ex[i]);
        char *b = msn_strs_join(n->backends, n->nbackends, ","), *r = mt_join(&runs, ",");
        *err = mt_fmt("backends %s: it runs %s", b, r);
        free(b), free(r);
        mt_strs_free(&runs);
        return false;
    }
    if (n->arch && strcmp(m->arch, n->arch) != 0) {
        *err = mt_fmt("arch %s: it is %s", n->arch, m->arch);
        return false;
    }
    if (n->min_mem_mb > 0) {
        if (m->ram_mb == 0) {
            *err = mt_fmt("min_mem_mb %u: its RAM is not reported yet", n->min_mem_mb);
            return false;
        }
        if (m->ram_mb < n->min_mem_mb) {
            *err = mt_fmt("min_mem_mb %u: it has %u MiB", n->min_mem_mb, m->ram_mb);
            return false;
        }
    }
    for (size_t i = 0; i < n->navoid; i++)
        if (msn_term_matches(&n->avoid[i], m)) {
            char *w = mesh_term_word(&n->avoid[i]);
            *err = mt_fmt("avoid %s", w);
            free(w);
            return false;
        }
    /* A GPU node must fit a block there (the CPU executors run a task on one lane: any shape loads). */
    if (strcmp(e, m->gpu) == 0 && (strcmp(e, "cuda") == 0 || strcmp(e, "metal") == 0) && m->max_threads > 0) {
        uint32_t threads = MESH_NODE_THREADS * (n->warps > 1 ? n->warps : 1);
        uint32_t smem = MESH_RT_SHARED_BYTES + n->shared;
        if (threads > m->max_threads) {
            *err = mt_fmt("warps %u: %u threads per node; its %s GPU allows %u", n->warps, threads, e, m->max_threads);
            return false;
        }
        if (smem > m->max_shared) {
            *err = mt_fmt("shared_bytes %u: %u bytes per node; its %s GPU allows %u", n->shared, smem, e, m->max_shared);
            return false;
        }
    }
    msm_set_str(exec, 16, e);
    return true;
}

bool msc_eligible(const msm_member *m, const mesh_needs *n) {
    char exec[16], *err = NULL;
    bool ok = msc_filter(m, n, exec, &err);
    free(err);
    return ok;
}

char *msc_explain(const msc_score_t *s) {
    mt_buf b = {0};
    mt_printf(&b, "%.3f = perf %.2f%s load %.2f mem %.2f spread %.2f", s->total, s->perf, s->explore ? "*" : "", s->load, s->mem, s->spread);
    if (s->rtt != 0.0) mt_printf(&b, " rtt %.2f", s->rtt);
    if (s->prefer != 0.0) mt_printf(&b, " prefer %+.2f", s->prefer);
    if (s->sticky != 0.0) mt_printf(&b, " sticky %.2f", s->sticky);
    if (s->owner != 0.0) mt_printf(&b, " owner %.2f", s->owner);
    mt_printf(&b, " (%s", s->exec);
    if (s->share.some)
        mt_printf(&b, ", %s share %.2f", strcmp(s->exec, "cuda") == 0 || strcmp(s->exec, "metal") == 0 ? "gpu" : "cpu", (double)s->share.v);
    if (s->task_us.some) mt_printf(&b, ", %.0f us/task", (double)s->task_us.v);
    mt_printf(&b, ", cpu after %.2f)", (double)s->cpu_after);
    return mt_take(&b);
}

msc_cx msc_cx_simple(const msw_weights *w, const msm_perf *here) {
    return (msc_cx){.w = w, .here = here, .best_task_us = here ? here->task_us : mof32_none(), .own_cpu = here ? here->cpu : 0.0f};
}

/* The GPU share assumed on a member with nothing observing it. */
#define SHARE_PRIOR 0.75f

msc_score_t msc_score_terms(const msm_member *m, const mesh_needs *n, const msc_cx *cx) {
    const msw_weights *w = cx->w;
    const char *e = msm_exec_for(m, n->backends, n->nbackends);
    msc_score_t s = {0};
    msm_set_str(s.exec, sizeof s.exec, e ? e : "");
    bool gpu = strcmp(s.exec, "cuda") == 0 || strcmp(s.exec, "metal") == 0;
    /* Speed: the best measured time per task over this member's (unknown: the optimistic prior, explored). */
    mof32 task = mof32_none();
    if (cx->here && strcmp(cx->here->exec, s.exec) == 0 && cx->here->task_us.some && cx->here->task_us.v > 0.0f) task = cx->here->task_us;
    double rel;
    if (task.some && cx->best_task_us.some && cx->best_task_us.v > 0.0f) rel = (double)fminf(cx->best_task_us.v / task.v, 1.0f);
    else if (task.some) rel = 1.0;
    else {
        s.explore = true;
        rel = 1.0;
    }
    s.task_us = task;
    /* CPU headroom after the move (the program's own CPU added where it does not run yet). */
    float cpus = (float)(m->cpus > 1 ? m->cpus : 1);
    float add = cx->current ? 0.0f : fmaxf(cx->own_cpu, n->cpu) / cpus;
    s.cpu_after = clampf(m->cpu_util + add, 0.0f, 1.0f);
    float psi = m->psi_cpu.some ? m->psi_cpu.v : 0.0f;
    /* The share of the processor it would get: the GPU share (GPU programs), else the time runnable
     * work is not stalled for a CPU (1 - CPU PSI; without PSI, a saturated host halves it). */
    float share;
    if (gpu) share = m->gpu_share.some ? m->gpu_share.v : SHARE_PRIOR;
    else share = m->psi_cpu.some ? 1.0f - m->psi_cpu.v : (s.cpu_after >= 0.98f ? 0.5f : 1.0f);
    share = clampf(share, 0.0f, 1.0f);
    s.share = mof32_some(share);
    s.perf = w->perf * rel * (double)share;
    /* CPU headroom, damped by CPU pressure (PSI 25 % and up: none). */
    float damp = 1.0f - clampf(psi / 0.25f, 0.0f, 1.0f);
    s.load = w->load * (double)((1.0f - s.cpu_after) * damp);
    /* Memory headroom (MemAvailable after the reservation), damped by memory pressure. */
    if (m->ram_mb > 0) {
        uint32_t res = cx->current ? 0 : n->mem_mb;
        float avail = (float)(m->mem_avail_mb > res ? m->mem_avail_mb - res : 0) / (float)m->ram_mb;
        float mp = 1.0f - clampf((m->psi_mem.some ? m->psi_mem.v : 0.0f) / 0.1f, 0.0f, 1.0f);
        s.mem = w->mem * (double)(clampf(avail, 0.0f, 1.0f) * mp);
    }
    double fill = (double)cx->placed / ((double)cx->max_placed + 1.0);
    s.spread = w->spread * (n->pack ? fill : 1.0 - fill);
    if (cx->rtt_ms.some) s.rtt = w->rtt * (1.0 - clampd((double)cx->rtt_ms.v / 2.0, 0.0, 1.0));
    size_t pref = 0;
    for (size_t i = 0; i < n->nprefer; i++)
        if (msn_term_matches(&n->prefer[i], m)) pref++;
    s.prefer = w->prefer * (double)pref;
    if (cx->current) s.sticky = w->sticky;
    if (m->busy) s.owner = -w->owner;
    s.total = fmax(s.perf + s.load + s.mem + s.spread + s.rtt + s.prefer + s.sticky + s.owner, 0.001);
    return s;
}

double msc_score(const msm_member *m, const mesh_needs *n, const msm_perf *here, const msw_weights *w) {
    msc_cx cx = msc_cx_simple(w, here);
    return msc_score_terms(m, n, &cx).total;
}
