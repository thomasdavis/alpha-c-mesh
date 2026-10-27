/* sched.rs: Input, Explain, order, members_of_at, Books, rows, sorted_ok, place, group_choice,
 * group_score. */
#include "mesh/sched_place.h"
#include "mesh/mt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t div_ceil(uint32_t a, uint32_t b) { return a / b + (a % b != 0); }

/* ---- explain */

static int row_order(const msp_row *a, const msp_row *b) {
    if (a->ok && b->ok) {
        if (b->score.total > a->score.total) return 1;
        if (b->score.total < a->score.total) return -1;
        return a->k < b->k ? -1 : a->k > b->k;
    }
    if (a->ok) return -1;
    if (b->ok) return 1;
    return a->k < b->k ? -1 : a->k > b->k;
}

char *msp_explain_text(const msp_explain *e) {
    mt_buf b = {0};
    char *on = e->chosen.some ? ml_text(&e->chosen.l) : NULL;
    if (on) mt_printf(&b, "program %s on %s: %s\n  needs %s\n", e->program, on, e->summary, e->needs);
    else mt_printf(&b, "program %s on every eligible member: %s\n  needs %s\n", e->program, e->summary, e->needs);
    free(on);
    /* A stable sort of the rows: feasible first, best score first, then by id. */
    size_t n = e->nrows;
    const msp_row **r = mt_alloc((n ? n : 1) * sizeof *r);
    for (size_t i = 0; i < n; i++) r[i] = &e->rows[i];
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && row_order(r[j - 1], r[j]) > 0; j--) {
            const msp_row *t = r[j - 1];
            r[j - 1] = r[j];
            r[j] = t;
        }
    for (size_t i = 0; i < n; i++) {
        bool chosen = ml_opt_has_or_all(&e->chosen, r[i]->k) && r[i]->ok;
        if (r[i]->ok) {
            char *x = msc_explain(&r[i]->score);
            mt_printf(&b, "  system %u %s score %s\n", r[i]->k, chosen ? "chosen  " : "feasible", x);
            free(x);
        } else mt_printf(&b, "  system %u rejected %s\n", r[i]->k, r[i]->why);
    }
    free(r);
    return mt_take(&b);
}

bool msp_score_of(const msp_explain *e, uint32_t k, double *score) {
    for (size_t i = 0; i < e->nrows; i++)
        if (e->rows[i].k == k) {
            if (!e->rows[i].ok) return false;
            *score = e->rows[i].score.total;
            return true;
        }
    return false;
}

void msp_rows_free(msp_row *rows, size_t n) {
    for (size_t i = 0; i < n; i++) free(rows[i].why);
    free(rows);
}

void msp_explain_free(msp_explain *e) {
    free(e->program);
    free(e->needs);
    free(e->summary);
    msp_rows_free(e->rows, e->nrows);
    ml_opt_free(&e->chosen);
    *e = (msp_explain){0};
}

void msp_explains_free(msp_explains *e) {
    for (size_t i = 0; i < e->n; i++) msp_explain_free(&e->v[i]);
    free(e->v);
    *e = (msp_explains){0};
}

const msp_explain *msp_explains_get(const msp_explains *e, const char *program) {
    for (size_t i = 0; i < e->n; i++)
        if (strcmp(e->v[i].program, program) == 0) return &e->v[i];
    return NULL;
}

/* ---- order */

typedef struct {
    int64_t neg_priority;
    uint32_t depth;
    const char *name;
} okey;

static int okey_cmp(const void *a, const void *b) {
    const okey *x = a, *y = b;
    if (x->neg_priority != y->neg_priority) return x->neg_priority < y->neg_priority ? -1 : 1;
    if (x->depth != y->depth) return x->depth < y->depth ? -1 : 1;
    return strcmp(x->name, y->name);
}

mt_strs msp_order(const mfd_doc *doc) {
    okey *k = mt_alloc((doc->nprogs ? doc->nprogs : 1) * sizeof *k);
    for (size_t i = 0; i < doc->nprogs; i++) {
        uint32_t depth = 0;
        const char *cur = doc->pname[i];
        mt_strs seen = {0};
        for (;;) {
            const mfd_prog *p = mfd_prog_get(doc, cur);
            if (!p || !p->needs.colocate_with) break;
            const char *a = p->needs.colocate_with;
            if (!mfd_prog_get(doc, a) || mt_strs_has(&seen, a)) break;
            mt_push(&seen, a);
            depth++;
            cur = a;
        }
        mt_strs_free(&seen);
        k[i] = (okey){-(int64_t)doc->prog[i].needs.priority, depth, doc->pname[i]};
    }
    qsort(k, doc->nprogs, sizeof *k, okey_cmp);
    mt_strs out = {0};
    for (size_t i = 0; i < doc->nprogs; i++) mt_push(&out, k[i].name);
    free(k);
    return out;
}

ml_list msp_members_of_at(const ml_opt *at, const msm_member *ms, size_t nms, const mesh_needs *n) {
    if (at->some) return ml_clone(&at->l);
    ml_list l = {0};
    for (size_t i = 0; i < nms; i++)
        if (msc_eligible(&ms[i], n)) ml_push(&l, ms[i].id);
    return l;
}

/* ---- books */

msp_books msp_books_new(size_t n) {
    size_t c = n ? n : 1;
    return (msp_books){n, mt_alloc(c * sizeof(uint32_t)), mt_alloc(c * sizeof(uint32_t)), mt_alloc(c * sizeof(uint32_t)), mt_alloc(c * sizeof(uint32_t)),
                       mt_alloc(c * sizeof(float))};
}

void msp_books_free(msp_books *b) {
    free(b->placed), free(b->mem), free(b->view_kb), free(b->nnew), free(b->cpu);
    *b = (msp_books){0};
}

void msp_books_add(msp_books *b, uint32_t k, const mesh_needs *n) {
    if (k >= b->n) return;
    b->placed[k] += 1;
    b->cpu[k] += n->cpu;
    b->mem[k] += n->mem_mb;
}

/* A placement made in this pass on a member that does not hold it yet: its view and slot. */
static void books_add_new(msp_books *b, uint32_t k, const mesh_needs *n) {
    if (k >= b->n) return;
    b->view_kb[k] += div_ceil(n->view_bytes, 1024);
    b->nnew[k] += 1;
}

static uint32_t books_max(const msp_books *b) {
    uint32_t m = 0;
    for (size_t i = 0; i < b->n; i++)
        if (b->placed[i] > m) m = b->placed[i];
    return m;
}

/* The reservations fit (the budget is the member's CPUs and RAM less the reserve), and the view and
 * a slot (a member that holds it already has both). */
static bool books_fits(const msp_books *b, const msm_member *m, const mesh_needs *n, const msw_weights *w, bool current, char **err) {
    uint32_t i = m->id;
    if (!current && m->heap_kb > 0) {
        uint32_t want = div_ceil(n->view_bytes, 1024) + b->view_kb[i];
        if (want > m->heap_kb) {
            char *extra = b->view_kb[i] > 0 ? mt_fmt(" (%u KiB placed there in this pass)", b->view_kb[i]) : mt_strdup("");
            *err = mt_fmt("view heap: %u KiB wanted, its largest free run is %u KiB%s", div_ceil(n->view_bytes, 1024), m->heap_kb, extra);
            free(extra);
            return false;
        }
        if (m->slots_free <= b->nnew[i]) {
            *err = mt_fmt("no free program slot (%u free)", m->slots_free);
            return false;
        }
    }
    float cpu = current ? b->cpu[i] - n->cpu : b->cpu[i];
    uint32_t mem = current ? (b->mem[i] > n->mem_mb ? b->mem[i] - n->mem_mb : 0) : b->mem[i];
    if (n->cpu > 0.0f) {
        float budget = (float)m->cpus * (1.0f - w->reserve);
        if (cpu + n->cpu > budget + 1e-6f) {
            *err = mt_fmt("cpu %.2f: %.2f of %.2f cores reserved (%u CPUs less %.0f %%)", (double)n->cpu, (double)cpu, (double)budget, m->cpus, (double)(w->reserve * 100.0f));
            return false;
        }
    }
    if (n->mem_mb > 0) {
        uint32_t budget = (uint32_t)((float)m->ram_mb * (1.0f - w->reserve));
        if (mem + n->mem_mb > budget) {
            *err = mt_fmt("mem_mb %u: %u of %u MiB reserved", n->mem_mb, mem, budget);
            return false;
        }
    }
    return true;
}

static mof32 best_task_us(const msm_perfs *perf, const char *name, uint64_t hash) {
    size_t lo, hi;
    msm_perfs_range(perf, name, &lo, &hi);
    mof32 best = mof32_none();
    for (size_t i = lo; i < hi; i++) {
        const msm_perf *p = &perf->perf[i];
        if (p->hash != hash || !p->task_us.some || !(p->task_us.v > 0.0f)) continue;
        best = best.some ? mof32_some(fminf(best.v, p->task_us.v)) : p->task_us;
    }
    return best;
}

/* The CPU a program uses: measured where it runs (the most of its replicas), else its reservation. */
static float own_cpu(const msm_perfs *perf, const char *name, uint64_t hash, const mesh_needs *n) {
    size_t lo, hi;
    msm_perfs_range(perf, name, &lo, &hi);
    float c = n->cpu;
    for (size_t i = lo; i < hi; i++)
        if (perf->perf[i].hash == hash && perf->perf[i].run) c = fmaxf(c, perf->perf[i].cpu);
    return c;
}

void msp_rows(const msp_input *in, const char *name, const ml_list *current, const msp_books *books, const ml_list *anchor, msp_row **rows, size_t *nrows) {
    const mfd_prog *p = mfd_prog_get(in->doc, name);
    const mesh_needs *n = &p->needs;
    mof32 best = best_task_us(in->perf, name, p->hash);
    float own = own_cpu(in->perf, name, p->hash, n);
    uint32_t maxp = books_max(books);
    msp_row *r = mt_alloc((in->nmembers ? in->nmembers : 1) * sizeof *r);
    for (size_t i = 0; i < in->nmembers; i++) {
        const msm_member *m = &in->members[i];
        bool cur = ml_has(current, m->id);
        char exec[16], *err = NULL;
        r[i].k = m->id;
        r[i].why = NULL;
        r[i].ok = false;
        if (!msc_filter(m, n, exec, &err)) {
            r[i].why = err;
            continue;
        }
        const char *f = ms_get(&m->fails, name);
        if (f) {
            r[i].why = mt_fmt("failed here: %s", f);
            continue;
        }
        if (!books_fits(books, m, n, in->w, cur, &err)) {
            r[i].why = err;
            continue;
        }
        if (anchor && !ml_has(anchor, m->id)) {
            r[i].why = mt_fmt("colocate_with %s: not there", n->colocate_with ? n->colocate_with : "?");
            continue;
        }
        const msm_perf *here = msm_perfs_get(in->perf, name, m->id);
        if (here && here->hash != p->hash) here = NULL;
        uint32_t placed = books->placed[m->id] > (uint32_t)cur ? books->placed[m->id] - (uint32_t)cur : 0;
        msc_cx cx = {.w = in->w, .current = cur, .here = here, .best_task_us = best, .own_cpu = own, .placed = placed, .max_placed = maxp};
        r[i].ok = true;
        r[i].score = msc_score_terms(m, n, &cx);
    }
    *rows = r;
    *nrows = in->nmembers;
}

size_t msp_sorted_ok(const msp_row *rows, size_t n, uint32_t *ks, double *totals) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++)
        if (rows[i].ok) {
            /* insertion, stable: best total first, then the lower id */
            size_t j = c++;
            while (j > 0 && (totals[j - 1] < rows[i].score.total || (totals[j - 1] == rows[i].score.total && ks[j - 1] > rows[i].k))) {
                ks[j] = ks[j - 1];
                totals[j] = totals[j - 1];
                j--;
            }
            ks[j] = rows[i].k;
            totals[j] = rows[i].score.total;
        }
    return c;
}

/* ---- place */

static bool row_valid(const msp_row *r, size_t n, uint32_t k) {
    for (size_t i = 0; i < n; i++)
        if (r[i].k == k && r[i].ok) return true;
    return false;
}

static bool grace(const msp_input *in, uint32_t k) {
    return k < in->nmembers && !in->members[k].up && in->members[k].down_ms < in->w->replace_ms;
}

static double score_or(const msp_explain *e, uint32_t k, double d) {
    double s;
    return msp_score_of(e, k, &s) ? s : d;
}

mfd_doc msp_place(const msp_input *in, mt_strs *changes, msp_explains *explains) {
    mfd_doc doc = mfd_doc_clone(in->doc);
    *changes = (mt_strs){0};
    *explains = (msp_explains){0};
    msp_books books = msp_books_new(in->nmembers);
    const msm_member *ms = in->members;
    size_t nms = in->nmembers;
    mt_strs ord = msp_order(in->doc);
    for (size_t oi = 0; oi < ord.n; oi++) {
        const char *name = ord.v[oi];
        mfd_prog p = mfd_prog_clone(mfd_prog_get(&doc, name));
        const mesh_needs *n = &p.needs;
        /* Where it runs now (a new program with replicas N: nowhere yet). */
        ml_list cur = (!p.at.some && !n->replicas.all && !n->colocate_with) ? (ml_list){0} : msp_members_of_at(&p.at, ms, nms, n);
        /* Colocated: the anchor's members (an anchor not in the document: no constraint). */
        bool has_anchor = false;
        ml_list anchor = {0};
        if (n->colocate_with) {
            const mfd_prog *a = mfd_prog_get(&doc, n->colocate_with);
            if (a) {
                has_anchor = true;
                anchor = msp_members_of_at(&a->at, ms, nms, &a->needs);
            }
        }
        msp_input view = {&doc, ms, nms, in->perf, in->w};
        msp_row *r;
        size_t nr;
        msp_rows(&view, name, &cur, &books, has_anchor ? &anchor : NULL, &r, &nr);
        msp_explain ex = {mt_strdup(name), msn_summary(n), NULL, NULL, 0, ml_opt_clone(&p.at)};
        ex.rows = mt_alloc((nr ? nr : 1) * sizeof *ex.rows);
        for (size_t i = 0; i < nr; i++) ex.rows[i] = (msp_row){r[i].k, r[i].ok, r[i].score, r[i].why ? mt_strdup(r[i].why) : NULL};
        ex.nrows = nr;
        uint32_t *ok_k = mt_alloc((nr ? nr : 1) * sizeof *ok_k);
        double *ok_s = mt_alloc((nr ? nr : 1) * sizeof *ok_s);
        size_t nok = msp_sorted_ok(r, nr, ok_k, ok_s);
        ml_opt want = ml_opt_clone(&p.at);
        char *why = mt_strdup(p.why);
        /* A pin of a program colocated with another (the anchor) is not applied: it would split the pair. */
        const ml_opt *pin = p.pin.some && !has_anchor ? &p.pin : NULL;
        if (p.has_from) {
            ex.summary = mt_fmt("migrating from system %u (the leader runs it)", p.from);
        } else if (pin) {
            bool moving = false;
            if (p.at.some && !ml_eq(&p.at.l, &pin->l) && p.at.l.n && n->migrate == MESH_MIGRATE_STATE) {
                moving = true;
                for (size_t i = 0; i < p.at.l.n; i++)
                    if (p.at.l.v[i] >= nms || !ms[p.at.l.v[i]].up) moving = false;
            }
            char *pl = ml_text(&pin->l);
            if (moving) ex.summary = mt_fmt("pinned by the operator to %s: the leader migrates it there (with its view)", pl);
            else {
                ml_opt_free(&want);
                want = ml_opt_clone(pin);
                free(why);
                why = mt_fmt("pinned by the operator to %s", pl);
                ex.summary = mt_strdup(why);
            }
            free(pl);
        } else if (n->lockstep) {
            ml_opt_free(&want);
            ex.summary = mt_strdup("the lockstep program: loaded on every eligible member; its group's members run it");
        } else if (has_anchor) {
            ml_list v = {0}, lost = {0};
            for (size_t i = 0; i < anchor.n; i++) {
                uint32_t k = anchor.v[i];
                bool valid = row_valid(r, nr, k), g = grace(in, k);
                if (valid || (g && p.at.some && ml_has(&p.at.l, k))) ml_push(&v, k);
                if (!valid && !g) ml_push(&lost, k);
            }
            char *al = ml_text(&anchor), *ll = ml_text(&lost);
            char *extra = lost.n ? mt_fmt("; cannot run on %s", ll) : mt_strdup("");
            ex.summary = mt_fmt("colocated with %s (on %s)%s", n->colocate_with, al, extra);
            free(al), free(ll), free(extra);
            if (!(p.at.some && ml_eq(&p.at.l, &v))) {
                free(why);
                why = mt_strdup(ex.summary);
            }
            ml_opt_free(&want);
            want = (ml_opt){true, v};
            ml_free(&lost);
        } else if (n->replicas.all) {
            ml_opt_free(&want);
            ex.summary = mt_fmt("replicas all: on the %zu eligible member(s)", nok);
        } else {
            uint32_t k = n->replicas.n;
            ml_list keep = {0}, dropped = {0}, added = {0};
            if (p.at.some)
                for (size_t i = 0; i < p.at.l.n; i++) {
                    uint32_t m = p.at.l.v[i];
                    if (row_valid(r, nr, m) || grace(in, m)) ml_push(&keep, m);
                }
            if (p.at.some)
                for (size_t i = 0; i < p.at.l.n; i++)
                    if (!ml_has(&keep, p.at.l.v[i])) ml_push(&dropped, p.at.l.v[i]);
            /* Too many: the lowest scores go (ties: the higher id). */
            while (keep.n > k) {
                size_t w = 0;
                for (size_t i = 1; i < keep.n; i++) {
                    double si = score_or(&ex, keep.v[i], -1.0), sw = score_or(&ex, keep.v[w], -1.0);
                    if (si < sw || (si == sw && keep.v[w] < keep.v[i])) w = i;
                }
                ml_remove_value(&keep, keep.v[w]);
            }
            for (size_t i = 0; i < nok && keep.n < k; i++)
                if (!ml_has(&keep, ok_k[i])) ml_push(&keep, ok_k[i]), ml_push(&added, ok_k[i]);
            ml_sort(&keep);
            mt_buf s = {0};
            if (!keep.n) {
                mt_strs rej = {0};
                for (size_t i = 0; i < nr; i++)
                    if (!r[i].ok) mt_push_own(&rej, mt_fmt("system %u: %s", r[i].k, r[i].why));
                char *j = mt_join(&rej, "; ");
                mt_printf(&s, "unplaced: no member fits (%s)", j);
                free(j);
                mt_strs_free(&rej);
            } else {
                mt_printf(&s, "replicas %u: ", k);
                for (size_t i = 0; i < keep.n; i++) mt_printf(&s, i ? ", system %u %.3f" : "system %u %.3f", keep.v[i], score_or(&ex, keep.v[i], 0.0));
                for (size_t i = 0; i < nok; i++)
                    if (!ml_has(&keep, ok_k[i])) {
                        mt_printf(&s, "; next system %u %.3f", ok_k[i], ok_s[i]);
                        break;
                    }
                if (dropped.n) {
                    char *dl = ml_text(&dropped);
                    mt_printf(&s, "; replaced %s (gone or no longer fits)", dl);
                    free(dl);
                }
                if (keep.n < k) mt_printf(&s, "; only %zu of %u placed", keep.n, k);
            }
            ex.summary = mt_take(&s);
            if (!(p.at.some && ml_eq(&p.at.l, &keep)) || added.n) {
                free(why);
                why = mt_strdup(ex.summary);
            }
            ml_opt_free(&want);
            want = (ml_opt){true, keep};
            ml_free(&dropped), ml_free(&added);
        }
        ml_list on = msp_members_of_at(&want, ms, nms, n);
        for (size_t i = 0; i < on.n; i++)
            if (on.v[i] < nms) {
                msp_books_add(&books, on.v[i], n);
                if (!ml_has(&cur, on.v[i])) books_add_new(&books, on.v[i], n);
            }
        ml_free(&on);
        ml_opt_free(&ex.chosen);
        ex.chosen = ml_opt_clone(&want);
        mfd_prog *e = mfd_prog_get(&doc, name);
        if (!ml_opt_eq(&want, &p.at)) {
            char *a = ml_at_word(&p.at), *b = ml_at_word(&want);
            mt_push_own(changes, mt_fmt("%s: %s -> %s (%s)", name, a, b, why));
            free(a), free(b);
            ml_opt_free(&e->at);
            e->at = want;
            want = ml_none();
            mt_set(&e->why, why);
        } else if (strcmp(why, p.why) != 0) {
            mt_set(&e->why, why);
        }
        ml_opt_free(&want);
        free(why);
        free(ok_k), free(ok_s);
        msp_rows_free(r, nr);
        ml_free(&cur), ml_free(&anchor);
        mfd_prog_free(&p);
        explains->v = mt_realloc(explains->v, (explains->n + 1) * sizeof *explains->v);
        explains->v[explains->n++] = ex;
    }
    mt_strs_free(&ord);
    msp_books_free(&books);
    return doc;
}

/* ---- groups */

/* The mean RTT in ms from member k to the chosen ones (those it has a figure for). */
static mof32 mean_rtt_ms(const msm_member *ms, size_t nms, uint32_t k, const uint32_t *to, size_t nto) {
    if (k >= nms) return mof32_none();
    float sum = -0.0f;
    size_t c = 0;
    for (size_t i = 0; i < nto; i++) {
        mof32 r = msm_rtt_to(&ms[k], to[i]);
        if (r.some) sum += r.v, c++;
    }
    if (!c) return mof32_none();
    return mof32_some(sum / (float)c / 1e3f);
}

bool msp_group_choice(const msp_input *in, const mfd_group *g, ml_list *members, char **why) {
    const mfd_prog *p = mfd_prog_get(in->doc, g->program);
    if (!p) return false;
    const mesh_needs *n = &p->needs;
    mof32 best = best_task_us(in->perf, g->program, p->hash);
    size_t nc = 0;
    uint32_t *ck = mt_alloc((in->nmembers ? in->nmembers : 1) * sizeof *ck);
    double *cs = mt_alloc((in->nmembers ? in->nmembers : 1) * sizeof *cs);
    for (size_t i = 0; i < in->nmembers; i++) {
        const msm_member *m = &in->members[i];
        if (!msc_eligible(m, n) || ms_get(&m->fails, g->program)) continue;
        bool match = n->ngroup_members == 0;
        for (size_t t = 0; t < n->ngroup_members; t++)
            if (msn_term_matches(&n->group_members[t], m)) match = true;
        if (!match) continue;
        const msm_perf *here = msm_perfs_get(in->perf, g->program, m->id);
        if (here && here->hash != p->hash) here = NULL;
        msc_cx cx = {.w = in->w, .current = ml_has(&g->members, m->id), .here = here, .best_task_us = best};
        ck[nc] = m->id;
        cs[nc++] = msc_score_terms(m, n, &cx).total;
    }
    if (!nc) {
        free(ck), free(cs);
        return false;
    }
    size_t size = n->group_size == 0 ? nc : (n->group_size < nc ? n->group_size : nc);
    /* left: the candidates by score, best first (ties: the lower id). */
    for (size_t i = 1; i < nc; i++)
        for (size_t j = i; j > 0 && (cs[j - 1] < cs[j] || (cs[j - 1] == cs[j] && ck[j - 1] > ck[j])); j--) {
            uint32_t tk = ck[j - 1];
            double ts = cs[j - 1];
            ck[j - 1] = ck[j], cs[j - 1] = cs[j];
            ck[j] = tk, cs[j] = ts;
        }
    uint32_t chosen_k[64];
    double chosen_s[64];
    size_t nch = 0, nleft = nc;
    chosen_k[nch] = ck[0], chosen_s[nch++] = cs[0];
    memmove(ck, ck + 1, (nleft - 1) * sizeof *ck);
    memmove(cs, cs + 1, (nleft - 1) * sizeof *cs);
    nleft--;
    while (nch < size) {
        /* The next member: its score plus the RTT term to those chosen (ties: the lower id). */
        size_t bi = 0;
        double bv = 0;
        for (size_t i = 0; i < nleft; i++) {
            mof32 r = mean_rtt_ms(in->members, in->nmembers, ck[i], chosen_k, nch);
            double x = (double)(r.some ? r.v : 0.0f) / 2.0;
            double v = cs[i] + in->w->rtt * (1.0 - (x < 0 ? 0 : x > 1 ? 1 : x));
            if (i == 0 || v > bv || (v == bv && ck[i] < ck[bi])) bi = i, bv = v;
        }
        chosen_k[nch] = ck[bi], chosen_s[nch++] = cs[bi];
        memmove(ck + bi, ck + bi + 1, (nleft - bi - 1) * sizeof *ck);
        memmove(cs + bi, cs + bi + 1, (nleft - bi - 1) * sizeof *cs);
        nleft--;
    }
    *members = ml_of(chosen_k, nch);
    ml_sort(members);
    mt_buf b = {0};
    mt_printf(&b, "%zu of %zu eligible (", nch, nc);
    for (size_t i = 0; i < nch; i++) mt_printf(&b, i ? ", system %u %.3f" : "system %u %.3f", chosen_k[i], chosen_s[i]);
    mt_putc(&b, ')');
    if (n->group_size > 0) mt_printf(&b, "; group_size %u", n->group_size);
    *why = mt_take(&b);
    free(ck), free(cs);
    return true;
}

double msp_group_score(const msp_input *in, const mfd_group *g, const ml_list *members) {
    const mfd_prog *p = mfd_prog_get(in->doc, g->program);
    if (!p) return 0.0;
    /* Rust's float sum starts at -0.0 (an empty group scores -0.0, printed "-0.000"). */
    double sum = -0.0;
    for (size_t i = 0; i < members->n; i++) {
        uint32_t k = members->v[i];
        if (k >= in->nmembers || !msc_eligible(&in->members[k], &p->needs)) continue;
        msc_cx cx = msc_cx_simple(in->w, msm_perfs_get(in->perf, g->program, in->members[k].id));
        sum += msc_score_terms(&in->members[k], &p->needs, &cx).total;
    }
    return sum;
}
