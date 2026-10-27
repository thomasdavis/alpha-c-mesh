/* Member, Perf and the performance map (see the header). */
#include "mesh/sched_member.h"
#include "mesh/text.h"

#include <stdlib.h>
#include <string.h>

msm_member msm_member_default(void) { return (msm_member){0}; }

void msm_set_str(char *dst, size_t cap, const char *s) {
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

msm_member msm_member_clone(const msm_member *m) {
    msm_member c = *m;
    c.busy = m->busy ? mt_strdup(m->busy) : NULL;
    c.fails = (ms_map){0};
    for (size_t i = 0; i < m->fails.n; i++) ms_set(&c.fails, m->fails.k[i], m->fails.v[i], true);
    return c;
}

void msm_member_free(msm_member *m) {
    free(m->busy);
    m->busy = NULL;
    ms_free(&m->fails);
}

size_t msm_executors(const msm_member *m, const char *out[3]) {
    size_t n = 0;
    out[n++] = m->gpu;
    const char *cpu[2] = {"wasm", "native"};
    for (int i = 0; i < 2; i++) {
        bool have = false;
        for (size_t j = 0; j < n; j++)
            if (strcmp(out[j], cpu[i]) == 0) have = true;
        if (!have) out[n++] = cpu[i];
    }
    return n;
}

const char *msm_exec_for(const msm_member *m, char *const *backends, size_t nb) {
    const char *ex[3];
    size_t n = msm_executors(m, ex);
    for (size_t i = 0; i < nb; i++)
        for (size_t j = 0; j < n; j++)
            if (strcmp(backends[i], ex[j]) == 0) return backends[i];
    return NULL;
}

mof32 msm_rtt_to(const msm_member *m, uint32_t k) {
    if (k == m->id) return mof32_some(0.0f);
    if (k >= m->nrtt) return mof32_none();
    return m->rtt_us[k];
}

bool msm_perf_eq(const msm_perf *a, const msm_perf *b) {
    return a->hash == b->hash && strcmp(a->exec, b->exec) == 0 && a->run == b->run && mof32_eq(a->task_us, b->task_us) && a->tasks_s == b->tasks_s &&
           a->cpu == b->cpu && mof32_eq(a->share, b->share) && a->rss_mb == b->rss_mb;
}

static int key_cmp(const char *an, uint32_t am, const char *bn, uint32_t bm) {
    int c = strcmp(an, bn);
    if (c) return c;
    return am < bm ? -1 : am > bm;
}

/* The index of (name, member), or where it goes. */
static size_t find(const msm_perfs *p, const char *name, uint32_t member, bool *found) {
    size_t lo = 0, hi = p->n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = key_cmp(p->name[mid], p->member[mid], name, member);
        if (c == 0) {
            *found = true;
            return mid;
        }
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    *found = false;
    return lo;
}

const msm_perf *msm_perfs_get(const msm_perfs *p, const char *name, uint32_t member) {
    bool f;
    size_t i = find(p, name, member, &f);
    return f ? &p->perf[i] : NULL;
}

void msm_perfs_set(msm_perfs *p, const char *name, uint32_t member, const msm_perf *perf) {
    bool f;
    size_t i = find(p, name, member, &f);
    if (f) {
        p->perf[i] = *perf;
        return;
    }
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 8;
        p->name = mt_realloc(p->name, p->cap * sizeof *p->name);
        p->member = mt_realloc(p->member, p->cap * sizeof *p->member);
        p->perf = mt_realloc(p->perf, p->cap * sizeof *p->perf);
    }
    memmove(p->name + i + 1, p->name + i, (p->n - i) * sizeof *p->name);
    memmove(p->member + i + 1, p->member + i, (p->n - i) * sizeof *p->member);
    memmove(p->perf + i + 1, p->perf + i, (p->n - i) * sizeof *p->perf);
    p->name[i] = mt_strdup(name);
    p->member[i] = member;
    p->perf[i] = *perf;
    p->n++;
}

void msm_perfs_range(const msm_perfs *p, const char *name, size_t *lo, size_t *hi) {
    bool f;
    *lo = find(p, name, 0, &f);
    size_t i = *lo;
    while (i < p->n && strcmp(p->name[i], name) == 0) i++;
    *hi = i;
}

void msm_perfs_remove_at(msm_perfs *p, size_t i) {
    free(p->name[i]);
    memmove(p->name + i, p->name + i + 1, (p->n - i - 1) * sizeof *p->name);
    memmove(p->member + i, p->member + i + 1, (p->n - i - 1) * sizeof *p->member);
    memmove(p->perf + i, p->perf + i + 1, (p->n - i - 1) * sizeof *p->perf);
    p->n--;
}

msm_perfs msm_perfs_clone(const msm_perfs *p) {
    msm_perfs c = {0};
    for (size_t i = 0; i < p->n; i++) msm_perfs_set(&c, p->name[i], p->member[i], &p->perf[i]);
    return c;
}

void msm_perfs_free(msm_perfs *p) {
    for (size_t i = 0; i < p->n; i++) free(p->name[i]);
    free(p->name);
    free(p->member);
    free(p->perf);
    *p = (msm_perfs){0};
}
