/* sched.rs: Move, Tracker, unit, start_move, resume_moves, MigOutcome, mig_verdict, end_move. */
#include "mesh/sched_tracker.h"
#include "mesh/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mst_move_free(mst_move *m) {
    mt_strs_free(&m->programs);
    free(m->why);
    *m = (mst_move){0};
}

mst_move mst_move_clone(const mst_move *m) { return (mst_move){mt_strs_clone(&m->programs), m->from, m->to, m->state, mt_strdup(m->why), m->forced}; }

bool mst_move_eq(const mst_move *a, const mst_move *b) {
    return mt_strs_eq(&a->programs, &b->programs) && a->from == b->from && a->to == b->to && a->state == b->state && strcmp(a->why, b->why) == 0 &&
           a->forced == b->forced;
}

typedef struct {
    char *name;
    uint32_t from;
    uint64_t since;
} cand_t;

typedef struct {
    uint32_t k;
    uint64_t since;
    bool timing, busy;
} owner_t;

struct mst_tracker {
    cand_t *cand; /* sorted by name */
    size_t ncand;
    ms_map last_move; /* name -> decimal ms */
    uint64_t *moves;  /* a queue of start times */
    size_t nmoves;
    owner_t owner[64]; /* by member */
    size_t nowner;
    bool has_group;
    char *group_name;
    ml_list group_members;
    uint64_t group_since;
    mt_strs notes;
};

mst_tracker *mst_new(void) { return mt_alloc(sizeof(mst_tracker)); }

static void cand_remove_at(mst_tracker *t, size_t i) {
    free(t->cand[i].name);
    memmove(t->cand + i, t->cand + i + 1, (t->ncand - i - 1) * sizeof *t->cand);
    t->ncand--;
}

static long cand_find(const mst_tracker *t, const char *name) {
    for (size_t i = 0; i < t->ncand; i++)
        if (strcmp(t->cand[i].name, name) == 0) return (long)i;
    return -1;
}

static bool cand_remove(mst_tracker *t, const char *name) {
    long i = cand_find(t, name);
    if (i < 0) return false;
    cand_remove_at(t, (size_t)i);
    return true;
}

void mst_free(mst_tracker *t) {
    if (!t) return;
    while (t->ncand) cand_remove_at(t, t->ncand - 1);
    free(t->cand);
    ms_free(&t->last_move);
    free(t->moves);
    free(t->group_name);
    ml_free(&t->group_members);
    mt_strs_free(&t->notes);
    free(t);
}

const mt_strs *mst_notes(const mst_tracker *t) { return &t->notes; }

static owner_t *owner_entry(mst_tracker *t, uint32_t k, uint64_t now) {
    for (size_t i = 0; i < t->nowner; i++)
        if (t->owner[i].k == k) return &t->owner[i];
    t->owner[t->nowner] = (owner_t){k, now, false, false};
    return &t->owner[t->nowner++];
}

static void owner_remove(mst_tracker *t, uint32_t k) {
    for (size_t i = 0; i < t->nowner; i++)
        if (t->owner[i].k == k) {
            t->owner[i] = t->owner[--t->nowner];
            return;
        }
}

void mst_owner(mst_tracker *t, msm_member *ms, size_t n, uint64_t now, const msw_weights *w) {
    for (size_t i = 0; i < n; i++) {
        msm_member *m = &ms[i];
        if (!m->up || !m->load_known) {
            free(m->busy);
            m->busy = NULL;
            owner_remove(t, m->id);
            continue;
        }
        mof32 share = strcmp(m->gpu, "wasm") != 0 ? m->gpu_share : mof32_none();
        bool hot = (share.some && share.v < w->owner_share) || (m->psi_cpu.some && m->psi_cpu.v > w->owner_psi);
        bool cool = (!share.some || share.v > w->owner_share * 1.33f) && (!m->psi_cpu.some || m->psi_cpu.v < w->owner_psi * 0.6f);
        owner_t *e = owner_entry(t, m->id, now);
        bool want = e->busy ? !cool : hot;
        if (want != e->busy) {
            if (!e->timing) e->since = now, e->timing = true;
            else if (now - (now > e->since ? e->since : now) >= w->owner_ms) e->since = now, e->timing = false, e->busy = want;
        } else if (e->timing) e->since = now, e->timing = false;
        free(m->busy);
        m->busy = NULL;
        if (e->busy) {
            char s[32], p[32];
            if (share.some) snprintf(s, sizeof s, "%.2f", (double)share.v);
            else strcpy(s, "-");
            if (m->psi_cpu.some) snprintf(p, sizeof p, "%.3f", (double)m->psi_cpu.v);
            else strcpy(p, "-");
            m->busy = mt_fmt("other tenants load it (share %s, PSI cpu %s)", s, p);
        }
    }
}

uint32_t mst_recent(mst_tracker *t, uint64_t now) {
    size_t drop = 0;
    while (drop < t->nmoves && now - (now > t->moves[drop] ? t->moves[drop] : now) >= 60000) drop++;
    if (drop) memmove(t->moves, t->moves + drop, (t->nmoves - drop) * sizeof *t->moves);
    t->nmoves -= drop;
    return (uint32_t)t->nmoves;
}

static void last_move_set(mst_tracker *t, const char *name, uint64_t now) {
    char v[24];
    snprintf(v, sizeof v, "%llu", (unsigned long long)now);
    ms_set(&t->last_move, name, v, true);
}

void mst_moved(mst_tracker *t, const mst_move *m, uint64_t now) {
    t->moves = mt_realloc(t->moves, (t->nmoves + 1) * sizeof *t->moves);
    t->moves[t->nmoves++] = now;
    for (size_t i = 0; i < m->programs.n; i++) {
        last_move_set(t, m->programs.v[i], now);
        cand_remove(t, m->programs.v[i]);
    }
}

mt_strs mst_unit(const mfd_doc *doc, const char *name, uint32_t from) {
    mt_strs v = {0};
    mt_push(&v, name);
    for (size_t i = 0; i < v.n; i++) {
        char *a = mt_strdup(v.v[i]);
        for (size_t j = 0; j < doc->nprogs; j++) {
            const mfd_prog *p = &doc->prog[j];
            if (p->needs.colocate_with && strcmp(p->needs.colocate_with, a) == 0 && !mt_strs_has(&v, doc->pname[j]) && ml_opt_has_or_all(&p->at, from))
                mt_push(&v, doc->pname[j]);
        }
        free(a);
    }
    /* Stop order: a program that feeds another in the unit stops before it. */
    mt_strs out = {0};
    while (v.n) {
        size_t pick = 0;
        bool found = false;
        for (size_t i = 0; i < v.n && !found; i++) {
            bool fed = false;
            for (size_t j = 0; j < v.n; j++) {
                if (j == i || strcmp(v.v[j], v.v[i]) == 0) continue;
                const mfd_prog *o = mfd_prog_get(doc, v.v[j]);
                if (o && mt_strs_has(&o->needs.feeds, v.v[i])) fed = true;
            }
            if (!fed) pick = i, found = true;
        }
        mt_push(&out, v.v[pick]);
        mt_strs_remove(&v, pick);
    }
    mt_strs_free(&v);
    return out;
}

static char *list_text(const ml_list *l) { return ml_text(l); }

bool mst_step(mst_tracker *t, const msp_input *in, uint64_t now, bool busy, mst_move *out) {
    mt_strs_free(&t->notes);
    const mfd_doc *doc = in->doc;
    mt_strs ord = msp_order(doc);
    bool result = false;
    /* Forced: a pin the placement left for a migration. */
    for (size_t oi = 0; oi < ord.n; oi++) {
        const char *name = ord.v[oi];
        const mfd_prog *p = mfd_prog_get(doc, name);
        if (!p->pin.some || !p->at.some) continue;
        if (p->has_from || ml_eq(&p->pin.l, &p->at.l) || p->needs.migrate != MSN_STATE) continue;
        /* Colocated with a loaded anchor: it moves with the anchor's unit, never on its own pin. */
        if (p->needs.colocate_with && mfd_prog_get(doc, p->needs.colocate_with)) continue;
        long fi = -1, ti = -1;
        for (size_t i = 0; i < p->at.l.n && fi < 0; i++)
            if (!ml_has(&p->pin.l, p->at.l.v[i])) fi = (long)i;
        if (fi < 0) continue;
        for (size_t i = 0; i < p->pin.l.n && ti < 0; i++)
            if (!ml_has(&p->at.l, p->pin.l.v[i])) ti = (long)i;
        if (ti < 0) continue;
        char *pl = list_text(&p->pin.l);
        if (busy) {
            mt_push_own(&t->notes, mt_fmt("%s: pinned to %s: waits for the migration in flight", name, pl));
            free(pl);
            goto done;
        }
        uint32_t from = p->at.l.v[fi];
        *out = (mst_move){mst_unit(doc, name, from), from, p->pin.l.v[ti], true, mt_fmt("pinned by the operator to %s", pl), true};
        free(pl);
        result = true;
        goto done;
    }
    if (doc->sched_paused) {
        mt_push(&t->notes, "paused (`sched resume`)");
        goto done;
    }
    msp_books books = msp_books_new(in->nmembers);
    for (size_t oi = 0; oi < ord.n; oi++) {
        const mfd_prog *p = mfd_prog_get(doc, ord.v[oi]);
        ml_list on = msp_members_of_at(&p->at, in->members, in->nmembers, &p->needs);
        for (size_t i = 0; i < on.n; i++)
            if (on.v[i] < in->nmembers) msp_books_add(&books, on.v[i], &p->needs);
        ml_free(&on);
    }
    /* ready: (gain, move) */
    double *gain = NULL;
    mst_move *ready = NULL;
    size_t nready = 0;
    mt_strs seen = {0};
    for (size_t oi = 0; oi < ord.n; oi++) {
        const char *name = ord.v[oi];
        const mfd_prog *p = mfd_prog_get(doc, name);
        const msn_needs *n = &p->needs;
        bool movable = !n->replicas.all && !p->pin.some && !n->lockstep && n->migrate != MSN_NEVER && !p->has_from &&
                       (!n->colocate_with || !mfd_prog_get(doc, n->colocate_with));
        if (!p->at.some || !p->at.l.n || !movable) {
            cand_remove(t, name);
            continue;
        }
        mt_push(&seen, name);
        msp_row *r;
        size_t nr;
        msp_rows(in, name, &p->at.l, &books, NULL, &r, &nr);
        /* The worst-placed replica (ties: the higher id) against the best member it is not on. */
        long from_i = -1;
        double s_from = 0;
        uint32_t from = 0;
        for (size_t i = 0; i < p->at.l.n; i++) {
            uint32_t k = p->at.l.v[i];
            for (size_t j = 0; j < nr; j++)
                if (r[j].k == k && r[j].ok) {
                    double s = r[j].score.total;
                    if (from_i < 0 || s < s_from || (s == s_from && k > from)) from_i = (long)i, s_from = s, from = k;
                    break;
                }
        }
        if (from_i < 0) {
            msp_rows_free(r, nr);
            continue;
        }
        uint32_t *ks = mt_alloc((nr ? nr : 1) * sizeof *ks);
        double *ts = mt_alloc((nr ? nr : 1) * sizeof *ts);
        size_t nok = msp_sorted_ok(r, nr, ks, ts);
        long to_i = -1;
        for (size_t i = 0; i < nok && to_i < 0; i++)
            if (!ml_has(&p->at.l, ks[i])) to_i = (long)i;
        if (to_i < 0) {
            cand_remove(t, name);
            free(ks), free(ts);
            msp_rows_free(r, nr);
            continue;
        }
        uint32_t to = ks[to_i];
        double s_to = ts[to_i];
        free(ks), free(ts);
        msp_rows_free(r, nr);
        double g = s_to / s_from - 1.0;
        if (g <= in->w->improve) {
            if (cand_remove(t, name)) mt_push_own(&t->notes, mt_fmt("%s: the gain fell to %.0f %%: stays on %u", name, g * 100.0, from));
            continue;
        }
        long ci = cand_find(t, name);
        if (ci < 0) {
            t->cand = mt_realloc(t->cand, (t->ncand + 1) * sizeof *t->cand);
            t->cand[t->ncand] = (cand_t){mt_strdup(name), from, now};
            ci = (long)t->ncand++;
        }
        cand_t *c = &t->cand[ci];
        if (c->from != from) c->from = from, c->since = now;
        uint64_t held = now > c->since ? now - c->since : 0;
        const char *lm = ms_get(&t->last_move, name);
        bool has_dwell = lm != NULL;
        uint64_t dwell = 0;
        if (lm) {
            uint64_t tm = strtoull(lm, NULL, 10);
            dwell = now > tm ? now - tm : 0;
        }
        char *why = mt_fmt("system %u %.3f -> system %u %.3f (+%.0f %% for %.1f s)", from, s_from, to, s_to, g * 100.0, (double)held / 1e3);
        if (held < in->w->hold_ms) {
            mt_push_own(&t->notes, mt_fmt("%s: %s: holding (%llu ms needed)", name, why, (unsigned long long)in->w->hold_ms));
            free(why);
            continue;
        }
        if (has_dwell && dwell < in->w->dwell_ms) {
            mt_push_own(&t->notes, mt_fmt("%s: %s: moved %.1f s ago (dwell %llu ms)", name, why, (double)dwell / 1e3, (unsigned long long)in->w->dwell_ms));
            free(why);
            continue;
        }
        gain = mt_realloc(gain, (nready + 1) * sizeof *gain);
        ready = mt_realloc(ready, (nready + 1) * sizeof *ready);
        gain[nready] = g;
        ready[nready++] = (mst_move){mst_unit(doc, name, from), from, to, n->migrate == MSN_STATE, why, false};
    }
    for (size_t i = t->ncand; i-- > 0;)
        if (!mt_strs_has(&seen, t->cand[i].name)) cand_remove_at(t, i);
    mt_strs_free(&seen);
    uint32_t recent = mst_recent(t, now);
    /* The biggest gain first (ties: the name). */
    size_t best = 0;
    for (size_t i = 1; i < nready; i++)
        if (gain[i] > gain[best] || (gain[i] == gain[best] && strcmp(ready[i].programs.v[0], ready[best].programs.v[0]) < 0)) best = i;
    if (nready) {
        mst_move *m = &ready[best];
        if (busy) mt_push_own(&t->notes, mt_fmt("%s: %s: waits for the migration in flight", m->programs.v[0], m->why));
        else if (recent >= in->w->per_min)
            mt_push_own(&t->notes, mt_fmt("%s: %s: budget spent (%u migrations in the last minute, at most %u)", m->programs.v[0], m->why, recent, in->w->per_min));
        else {
            *out = *m;
            *m = (mst_move){0};
            result = true;
        }
    }
    for (size_t i = 0; i < nready; i++) mst_move_free(&ready[i]);
    free(ready);
    free(gain);
    msp_books_free(&books);
done:
    mt_strs_free(&ord);
    return result;
}

bool mst_group_step(mst_tracker *t, const msp_input *in, uint64_t now, char **group, ml_list *members, char **why) {
    if (!in->doc->ngroups) return false;
    const mfd_group *g = &in->doc->groups[0];
    ml_list ideal;
    char *w;
    if (!msp_group_choice(in, g, &ideal, &w)) return false;
    bool ok = false;
    if (ml_eq(&ideal, &g->members)) {
        t->has_group = false;
        goto done;
    }
    double cur = msp_group_score(in, g, &g->members), nw = msp_group_score(in, g, &ideal);
    bool lost = false;
    for (size_t i = 0; i < g->members.n; i++)
        if (g->members.v[i] >= in->nmembers || !in->members[g->members.v[i]].up) lost = true;
    if (!lost && nw <= cur * (1.0 + in->w->improve) && ideal.n <= g->members.n) {
        t->has_group = false;
        goto done;
    }
    uint64_t since;
    if (t->has_group && strcmp(t->group_name, g->name) == 0 && ml_eq(&t->group_members, &ideal)) since = t->group_since;
    else {
        mt_set(&t->group_name, g->name);
        ml_free(&t->group_members);
        t->group_members = ml_clone(&ideal);
        t->group_since = now;
        t->has_group = true;
        since = now;
    }
    if ((now > since ? now - since : 0) < in->w->hold_ms) goto done;
    char *l = ml_text(&g->members);
    *why = mt_fmt("%s; now %s scores %.3f, proposed %.3f", w, l, cur, nw);
    free(l);
    *group = mt_strdup(g->name);
    *members = ideal;
    ideal = (ml_list){0};
    ok = true;
done:
    ml_free(&ideal);
    free(w);
    return ok;
}

mfd_doc mst_start_move(const mfd_doc *doc, const mst_move *m, uint64_t since) {
    mfd_doc d = mfd_doc_clone(doc);
    for (size_t i = 0; i < m->programs.n; i++) {
        mfd_prog *p = mfd_prog_get(&d, m->programs.v[i]);
        if (!p) continue;
        ml_list at = p->at.some ? ml_clone(&p->at.l) : (ml_list){0};
        ml_remove_value(&at, m->from);
        if (!ml_has(&at, m->to)) ml_push(&at, m->to);
        ml_sort(&at);
        ml_opt_free(&p->at);
        p->at = (ml_opt){true, at};
        p->has_from = p->has_to = m->state;
        p->from = m->state ? m->from : 0;
        p->to = m->state ? m->to : 0;
        p->since = m->state ? since : 0;
        free(p->why);
        p->why = mt_fmt("moved from system %u to %u: %s", m->from, m->to, m->why);
    }
    return d;
}

mst_resumed mst_resume_moves(const mfd_doc *doc) {
    mst_resumed r = {0};
    mt_strs ord = msp_order(doc);
    for (size_t oi = 0; oi < ord.n; oi++) {
        const mfd_prog *p = mfd_prog_get(doc, ord.v[oi]);
        if (!p->has_from || !p->has_to) continue;
        size_t i = 0;
        for (; i < r.n; i++)
            if (r.moves[i].from == p->from && r.moves[i].to == p->to && r.since[i] == p->since) break;
        if (i < r.n) mt_push(&r.moves[i].programs, ord.v[oi]);
        else {
            r.moves = mt_realloc(r.moves, (r.n + 1) * sizeof *r.moves);
            r.since = mt_realloc(r.since, (r.n + 1) * sizeof *r.since);
            mst_move m = {{0}, p->from, p->to, true, mt_strdup("taken over by a new leader"), false};
            mt_push(&m.programs, ord.v[oi]);
            r.moves[r.n] = m;
            r.since[r.n++] = p->since;
        }
    }
    mt_strs_free(&ord);
    return r;
}

void mst_resumed_free(mst_resumed *r) {
    for (size_t i = 0; i < r->n; i++) mst_move_free(&r->moves[i]);
    free(r->moves);
    free(r->since);
    *r = (mst_resumed){0};
}

bool mst_mig_verdict(const mst_move *m, uint64_t since, bool target_up, const mst_outcome *outs, size_t nouts, const char *reason, uint64_t waited_ms,
                     uint64_t limit_ms, bool *ok, char **why, double *gap_ms) {
    *gap_ms = -1;
    if (!target_up) {
        *ok = false;
        *why = mt_fmt("the target, system %u, was lost", m->to);
        return true;
    }
    size_t got = 0;
    uint32_t gap = 0;
    mt_strs bad = {0};
    for (size_t i = 0; i < m->programs.n; i++)
        for (size_t j = 0; j < nouts; j++)
            if (strcmp(outs[j].name, m->programs.v[i]) == 0 && outs[j].version >= since) {
                got++;
                if (!outs[j].ok) mt_push(&bad, outs[j].name);
                if (outs[j].gap_us > gap) gap = outs[j].gap_us;
                break;
            }
    if (got == m->programs.n) {
        if (!bad.n) {
            char *names = mt_join(&m->programs, ",");
            *ok = true;
            *why = mt_fmt("system %u runs %s", m->to, names);
            free(names);
            if (gap > 0) *gap_ms = (double)gap / 1e3;
        } else {
            char *names = mt_join(&bad, ",");
            *ok = false;
            *why = mt_fmt("system %u could not take %s (%s)", m->to, names, reason);
            free(names);
        }
        mt_strs_free(&bad);
        return true;
    }
    mt_strs_free(&bad);
    if (waited_ms > limit_ms) {
        *ok = false;
        *why = mt_fmt("not done in %llu s", (unsigned long long)(limit_ms / 1000));
        return true;
    }
    return false;
}

mfd_doc mst_end_move(const mfd_doc *doc, const mst_move *m, bool ok, const char *why) {
    mfd_doc d = mfd_doc_clone(doc);
    for (size_t i = 0; i < m->programs.n; i++) {
        mfd_prog *p = mfd_prog_get(&d, m->programs.v[i]);
        if (!p) continue;
        p->has_from = p->has_to = false;
        p->from = p->to = 0;
        p->since = 0;
        if (!ok) {
            ml_list at = p->at.some ? ml_clone(&p->at.l) : (ml_list){0};
            ml_remove_value(&at, m->to);
            if (!ml_has(&at, m->from)) ml_push(&at, m->from);
            ml_sort(&at);
            ml_opt_free(&p->at);
            p->at = (ml_opt){true, at};
        }
        mt_set(&p->why, why);
    }
    return d;
}
