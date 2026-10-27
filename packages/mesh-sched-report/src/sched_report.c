/* sched.rs Report: encode and decode. */
#include "mesh/sched_report.h"
#include "mesh/parse.h"
#include "mesh/rust_fmt.h"
#include "mesh/mt.h"

#include <stdlib.h>
#include <string.h>

#define GROW(arr, n) ((arr) = mt_realloc((arr), ((n) + 1) * sizeof *(arr)))

void msr_add_prog(msr_report *r, const char *name, const msm_perf *p) {
    GROW(r->progs, r->nprogs);
    r->progs[r->nprogs++] = (msr_prog){mt_strdup(name), *p};
}

void msr_add_fail(msr_report *r, const char *name, uint64_t hash, bool unsupported, const char *why) {
    GROW(r->fails, r->nfails);
    r->fails[r->nfails++] = (msr_fail){mt_strdup(name), hash, unsupported, mt_strdup(why)};
}

void msr_add_rtt(msr_report *r, uint32_t k, float us) {
    GROW(r->rtt, r->nrtt);
    r->rtt[r->nrtt++] = (msr_rtt){k, us};
}

void msr_add_echo(msr_report *r, uint32_t k, uint64_t t, uint64_t held) {
    GROW(r->echo, r->necho);
    r->echo[r->necho++] = (msr_echo){k, t, held};
}

static void opt(mt_buf *b, mof32 v) {
    if (v.some) mt_printf(b, "%.2f", (double)v.v);
    else mt_putc(b, '-');
}

char *msr_encode(const msr_report *r) {
    mt_buf b = {0};
    mt_printf(&b, "t %llu\ncaps cpus %u big %u little %u ram_mb %u disk_mb %u heap_kb %u slots_free %u\n", (unsigned long long)r->t_us, r->cpus, r->big,
              r->little, r->ram_mb, r->disk_free_mb, r->heap_kb, r->slots_free);
    for (size_t i = 0; i < r->nprogs; i++) {
        const msm_perf *p = &r->progs[i].perf;
        mt_printf(&b, "prog %s hash %016llx exec %s run %u task_us ", r->progs[i].name, (unsigned long long)p->hash, p->exec, (unsigned)p->run);
        opt(&b, p->task_us);
        mt_printf(&b, " tasks_s %.2f cpu %.3f share ", (double)p->tasks_s, (double)p->cpu);
        opt(&b, p->share);
        mt_printf(&b, " rss_mb %.1f\n", (double)p->rss_mb);
    }
    for (size_t i = 0; i < r->nrtt; i++) mt_printf(&b, "rtt %u %.1f\n", r->rtt[i].k, (double)r->rtt[i].us);
    for (size_t i = 0; i < r->necho; i++)
        mt_printf(&b, "echo %u %llu %llu\n", r->echo[i].k, (unsigned long long)r->echo[i].t, (unsigned long long)r->echo[i].held);
    for (size_t i = 0; i < r->nfails; i++) {
        char *why = mt_strdup(r->fails[i].why);
        for (char *c = why; *c; c++)
            if (*c == '\n') *c = ' ';
        mt_printf(&b, "fail %s %016llx %u %s\n", r->fails[i].name, (unsigned long long)r->fails[i].hash, (unsigned)r->fails[i].unsupported, why);
        free(why);
    }
    return mt_take(&b);
}

/* Rust's `n(x)? as u32`: parsed as u64, then truncated. */
static bool n32(const char *s, uint32_t *out) {
    uint64_t v;
    if (!mp_u64(s, &v)) return false;
    *out = (uint32_t)v;
    return true;
}

static bool of(const char *s, mof32 *out) {
    if (strcmp(s, "-") == 0) {
        *out = mof32_none();
        return true;
    }
    float f;
    if (!mp_f32(s, &f)) return false;
    *out = mof32_some(f);
    return true;
}

static bool is(mt_strs *w, size_t n, const char *const *lit) {
    if (w->n != n) return false;
    for (size_t i = 0; i < n; i++)
        if (lit[i] && strcmp(w->v[i], lit[i]) != 0) return false;
    return true;
}

static bool line(msr_report *r, const char *l, mt_strs *w) {
    if (w->n == 0) return true;
    static const char *const t[] = {"t", NULL};
    static const char *const caps[] = {"caps", "cpus", NULL, "big", NULL, "little", NULL, "ram_mb", NULL, "disk_mb", NULL, "heap_kb", NULL, "slots_free", NULL};
    static const char *const prog[] = {"prog", NULL, "hash", NULL, "exec", NULL, "run", NULL, "task_us", NULL, "tasks_s", NULL, "cpu", NULL, "share", NULL, "rss_mb", NULL};
    static const char *const rtt[] = {"rtt", NULL, NULL};
    static const char *const echo[] = {"echo", NULL, NULL, NULL};
    char **v = w->v;
    if (is(w, 2, t)) return mp_u64(v[1], &r->t_us);
    if (is(w, 15, caps))
        return n32(v[2], &r->cpus) && n32(v[4], &r->big) && n32(v[6], &r->little) && n32(v[8], &r->ram_mb) && n32(v[10], &r->disk_free_mb) &&
               n32(v[12], &r->heap_kb) && n32(v[14], &r->slots_free);
    if (is(w, 18, prog)) {
        msm_perf p = {0};
        if (!mp_hex_u64(v[3], &p.hash)) return false;
        msm_set_str(p.exec, sizeof p.exec, v[5]);
        p.run = strcmp(v[7], "1") == 0;
        if (!of(v[9], &p.task_us) || !mp_f32(v[11], &p.tasks_s) || !mp_f32(v[13], &p.cpu) || !of(v[15], &p.share) || !mp_f32(v[17], &p.rss_mb)) return false;
        msr_add_prog(r, v[1], &p);
        return true;
    }
    if (is(w, 3, rtt)) {
        uint32_t k;
        float us;
        if (!n32(v[1], &k) || !mp_f32(v[2], &us)) return false;
        msr_add_rtt(r, k, us);
        return true;
    }
    if (is(w, 4, echo)) {
        uint32_t k;
        uint64_t a, b;
        if (!n32(v[1], &k) || !mp_u64(v[2], &a) || !mp_u64(v[3], &b)) return false;
        msr_add_echo(r, k, a, b);
        return true;
    }
    if (w->n >= 4 && strcmp(v[0], "fail") == 0) {
        uint64_t h;
        if (!mp_hex_u64(v[2], &h)) return false;
        /* Rust's l.splitn(5, ' ').nth(4): after the fourth single space. */
        const char *why = l;
        for (int i = 0; i < 4 && why; i++) {
            why = strchr(why, ' ');
            if (why) why++;
        }
        msr_add_fail(r, v[1], h, strcmp(v[3], "1") == 0, why ? why : "");
        return true;
    }
    return false;
}

bool msr_decode(const char *text, msr_report *out, char **err) {
    msr_report r = {0};
    mt_strs ls = mt_lines(text);
    bool ok = true;
    for (size_t i = 0; i < ls.n && ok; i++) {
        mt_strs w = mt_words(ls.v[i]);
        ok = line(&r, ls.v[i], &w);
        mt_strs_free(&w);
        if (!ok) {
            char *q = rf_debug_str(ls.v[i]);
            *err = mt_fmt("F_PERF: %s", q);
            free(q);
        }
    }
    mt_strs_free(&ls);
    if (!ok) {
        msr_free(&r);
        return false;
    }
    *out = r;
    return true;
}

bool msr_eq(const msr_report *a, const msr_report *b) {
    if (a->t_us != b->t_us || a->cpus != b->cpus || a->big != b->big || a->little != b->little || a->ram_mb != b->ram_mb || a->disk_free_mb != b->disk_free_mb ||
        a->heap_kb != b->heap_kb || a->slots_free != b->slots_free || a->nprogs != b->nprogs || a->nfails != b->nfails || a->nrtt != b->nrtt || a->necho != b->necho)
        return false;
    for (size_t i = 0; i < a->nprogs; i++)
        if (strcmp(a->progs[i].name, b->progs[i].name) != 0 || !msm_perf_eq(&a->progs[i].perf, &b->progs[i].perf)) return false;
    for (size_t i = 0; i < a->nfails; i++)
        if (strcmp(a->fails[i].name, b->fails[i].name) != 0 || a->fails[i].hash != b->fails[i].hash || a->fails[i].unsupported != b->fails[i].unsupported ||
            strcmp(a->fails[i].why, b->fails[i].why) != 0)
            return false;
    for (size_t i = 0; i < a->nrtt; i++)
        if (a->rtt[i].k != b->rtt[i].k || a->rtt[i].us != b->rtt[i].us) return false;
    for (size_t i = 0; i < a->necho; i++)
        if (a->echo[i].k != b->echo[i].k || a->echo[i].t != b->echo[i].t || a->echo[i].held != b->echo[i].held) return false;
    return true;
}

void msr_free(msr_report *r) {
    for (size_t i = 0; i < r->nprogs; i++) free(r->progs[i].name);
    for (size_t i = 0; i < r->nfails; i++) {
        free(r->fails[i].name);
        free(r->fails[i].why);
    }
    free(r->progs);
    free(r->fails);
    free(r->rtt);
    free(r->echo);
    *r = (msr_report){0};
}
