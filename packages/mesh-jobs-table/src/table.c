/* jobs.rs's Job and Table: apply (the semilattice), ops (a table as ops), next_id, fold. */
#include "mesh/jobs/table.h"

#include <stdlib.h>
#include <string.h>

/* ---- map value helpers ---------------------------------------------------------------------- */

static void res_free(void *v) { mj_res_free(v); }
static void res_copy(void *d, const void *s) { *(mj_res *)d = mj_res_clone(s); }
static void failed_free(void *v) { free(((mj_failed *)v)->why); }
static void failed_copy(void *d, const void *s) {
    const mj_failed *f = s;
    *(mj_failed *)d = (mj_failed){f->tries, mstrdup(f->why ? f->why : "")};
}
static void jobp_free(void *v) { mj_job_free(*(mj_job **)v); }
static void jobp_copy(void *d, const void *s) { *(mj_job **)d = mj_job_clone(*(mj_job *const *)s); }

static momap *counts(void) { return momap_new(1, sizeof(uint64_t), NULL, NULL); }

uint64_t mj_count_get(const momap *m, uint32_t member) {
    const uint64_t *v = momap_get1(m, member);
    return v ? *v : 0;
}

static void count_add(momap *m, uint32_t member, uint64_t n) {
    uint64_t *v = momap_put1(m, member, NULL);
    *v += n;
}

/* ---- Job -------------------------------------------------------------------------------------- */

mj_job *mj_job_new(const mj_spec *spec) {
    mj_job *j = mxalloc(sizeof *j);
    j->spec = mj_spec_clone(spec);
    j->results = momap_new(1, sizeof(mj_res), res_free, res_copy);
    j->runs = momap_new(1, sizeof(mj_run), NULL, NULL);
    j->failed = momap_new(1, sizeof(mj_failed), failed_free, failed_copy);
    j->fails = momap_new(1, sizeof(uint32_t), NULL, NULL);
    j->by_member = counts();
    j->folded = counts();
    return j;
}

void mj_job_free(mj_job *j) {
    if (!j) return;
    mj_spec_free(&j->spec);
    mj_ranges_free(&j->done);
    momap_free(j->results);
    momap_free(j->runs);
    momap_free(j->failed);
    momap_free(j->fails);
    momap_free(j->by_member);
    momap_free(j->folded);
    free(j->cancel);
    free(j);
}

mj_job *mj_job_clone(const mj_job *j) {
    mj_job *c = mxalloc(sizeof *c);
    *c = *j;
    c->spec = mj_spec_clone(&j->spec);
    c->done = mj_ranges_clone(&j->done);
    c->results = momap_clone(j->results);
    c->runs = momap_clone(j->runs);
    c->failed = momap_clone(j->failed);
    c->fails = momap_clone(j->fails);
    c->by_member = momap_clone(j->by_member);
    c->folded = momap_clone(j->folded);
    c->cancel = j->cancel ? mstrdup(j->cancel) : NULL;
    return c;
}

/* Two maps equal key by key, values compared by eq (or bytes). */
static bool map_eq(const momap *a, const momap *b, bool (*eq)(const void *, const void *)) {
    if (momap_len(a) != momap_len(b)) return false;
    uint64_t ka[MOMAP_MAX_KEY], kb[MOMAP_MAX_KEY];
    const void *va = momap_first(a, ka), *vb = momap_first(b, kb);
    unsigned kw = momap_key_words(a);
    while (va && vb) {
        if (memcmp(ka, kb, 8 * kw) != 0) return false;
        if (eq ? !eq(va, vb) : memcmp(va, vb, momap_val_size(a)) != 0) return false;
        va = momap_next(a, ka, ka);
        vb = momap_next(b, kb, kb);
    }
    return !va && !vb;
}

static bool res_eq(const void *a, const void *b) { return mj_res_eq(a, b); }
static bool run_eq(const void *a, const void *b) {
    const mj_run *x = a, *y = b;
    return x->end == y->end && x->lease == y->lease && x->member == y->member;
}
static bool failed_eq(const void *a, const void *b) {
    const mj_failed *x = a, *y = b;
    return x->tries == y->tries && strcmp(x->why, y->why) == 0;
}
static bool u32_eq(const void *a, const void *b) { return *(const uint32_t *)a == *(const uint32_t *)b; }
static bool u64_eq(const void *a, const void *b) { return *(const uint64_t *)a == *(const uint64_t *)b; }

bool mj_job_eq(const mj_job *a, const mj_job *b) {
    return mj_spec_eq(&a->spec, &b->spec) && mj_ranges_eq(&a->done, &b->done) && map_eq(a->results, b->results, res_eq) &&
           map_eq(a->runs, b->runs, run_eq) && map_eq(a->failed, b->failed, failed_eq) && map_eq(a->fails, b->fails, u32_eq) &&
           a->dupes == b->dupes && (a->cancel == NULL) == (b->cancel == NULL) && (!a->cancel || strcmp(a->cancel, b->cancel) == 0) &&
           a->finished == b->finished && (!a->finished || (a->finish_at == b->finish_at && a->finish_state == b->finish_state)) &&
           map_eq(a->by_member, b->by_member, u64_eq) && a->fold_end == b->fold_end && map_eq(a->folded, b->folded, u64_eq) &&
           a->tone_seq == b->tone_seq && a->tone == b->tone;
}

uint64_t mj_job_total(const mj_job *j) { return mj_ranges_len(&j->spec.items); }

bool mj_job_settled(const mj_job *j, uint64_t x) { return mj_ranges_contains(&j->done, x) || momap_get1(j->failed, x) != NULL; }

bool mj_job_lease_of(const mj_job *j, uint64_t x, uint64_t *lease) {
    const mj_res *r = momap_get1(j->results, x);
    if (r) {
        *lease = r->lease;
        return true;
    }
    const mj_run *run = momap_floor(j->runs, &x, NULL);
    if (run && x < run->end) {
        *lease = run->lease;
        return true;
    }
    return false;
}

bool mj_job_active(const mj_job *j) { return !j->finished && j->cancel == NULL; }

bool mj_job_complete(const mj_job *j) {
    if (mj_spec_endless(&j->spec)) return false;
    uint64_t d = mj_ranges_len(&j->done), f = momap_len(j->failed);
    uint64_t sum = d > UINT64_MAX - f ? UINT64_MAX : d + f;
    return sum >= mj_job_total(j);
}

const char *mj_job_state(const mj_job *j) {
    if (j->finished) {
        switch (j->finish_state) {
        case MESH_JOBS_FIN_DONE: return momap_len(j->failed) == 0 ? "done" : "done-with-failures";
        case MESH_JOBS_FIN_CANCELLED: return "cancelled";
        case MESH_JOBS_FIN_DEADLINE: return "deadline";
        default: break;
        }
    }
    return j->cancel ? "cancelling" : "running";
}

mj_ranges mj_job_left(const mj_job *j) {
    mj_ranges r = mj_ranges_clone(&j->spec.items);
    mj_ranges_subtract(&r, &j->done);
    uint64_t x;
    for (const void *v = momap_first(j->failed, &x); v; v = momap_next(j->failed, &x, &x)) mj_ranges_take(&r, x);
    return r;
}

/* Folds the runs below the done prefix (the items done from the job's first on, without a gap)
 * into per-member counts. Deterministic: every replica that applies the same ops folds the same. */
static void fold(mj_job *j) {
    uint64_t first;
    if (!mj_ranges_first(&j->spec.items, &first)) return;
    if (!j->done.n) return;
    uint64_t s = j->done.v[0][0], w = j->done.v[0][1];
    if (s != first || w <= j->fold_end) return;
    momap *keep = momap_split_off(j->runs, &w);
    uint64_t a;
    for (const mj_run *r = momap_first(j->runs, &a); r; r = momap_next(j->runs, &a, &a)) count_add(j->folded, r->member, r->end - a);
    momap_free(j->runs);
    j->runs = keep;
    j->fold_end = w;
}

/* ---- Table ------------------------------------------------------------------------------------ */

void mj_table_init(mj_table *t) {
    memset(t, 0, sizeof *t);
    t->jobs = momap_new(1, sizeof(mj_job *), jobp_free, jobp_copy);
}

void mj_table_free(mj_table *t) {
    momap_free(t->jobs);
    free(t->forgotten);
    memset(t, 0, sizeof *t);
}

mj_table mj_table_clone(const mj_table *t) {
    mj_table c = {0};
    c.jobs = momap_clone(t->jobs);
    if (t->nforgotten) {
        c.forgotten = mxalloc(t->nforgotten * sizeof *c.forgotten);
        memcpy(c.forgotten, t->forgotten, t->nforgotten * sizeof *c.forgotten);
    }
    c.nforgotten = c.capforgotten = t->nforgotten;
    return c;
}

static bool jobp_eq(const void *a, const void *b) { return mj_job_eq(*(mj_job *const *)a, *(mj_job *const *)b); }

bool mj_table_eq(const mj_table *a, const mj_table *b) {
    return map_eq(a->jobs, b->jobs, jobp_eq) && a->nforgotten == b->nforgotten &&
           (a->nforgotten == 0 || memcmp(a->forgotten, b->forgotten, a->nforgotten * sizeof *a->forgotten) == 0);
}

mj_job *mj_table_get(const mj_table *t, mj_job_id id) {
    mj_job **p = momap_get1(t->jobs, id);
    return p ? *p : NULL;
}

bool mj_table_forgot(const mj_table *t, mj_job_id id) {
    for (size_t i = 0; i < t->nforgotten; i++)
        if (t->forgotten[i] == id) return true;
    return false;
}

mj_job *mj_table_first(const mj_table *t, uint64_t *id) {
    mj_job **p = momap_first(t->jobs, id);
    return p ? *p : NULL;
}

mj_job *mj_table_next(const mj_table *t, uint64_t *id) {
    mj_job **p = momap_next(t->jobs, id, id);
    return p ? *p : NULL;
}

static void done_run(mj_job *j, const mj_op *op) {
    mj_ranges span = mj_ranges_of(op->s, op->e);
    mj_ranges fresh = mj_ranges_intersect(&span, &j->spec.items);
    mj_ranges_free(&span);
    mj_ranges_subtract(&fresh, &j->done);
    uint64_t x;
    for (const void *v = momap_first(j->failed, &x); v; v = momap_next(j->failed, &x, &x)) mj_ranges_take(&fresh, x);
    for (size_t i = 0; i < fresh.n; i++) {
        uint64_t a = fresh.v[i][0], b = fresh.v[i][1];
        mj_ranges_insert(&j->done, a, b);
        count_add(j->by_member, op->member, b - a);
        /* Adjacent runs of one lease are one run. */
        uint64_t pa;
        const mj_run *p = momap_prev(j->runs, &a, &pa);
        if (p && p->end == a && p->lease == op->lease && p->member == op->member) {
            momap_del1(j->runs, pa);
            a = pa;
        }
        const mj_run *n = momap_get1(j->runs, b);
        if (n && n->lease == op->lease && n->member == op->member) {
            uint64_t nb = n->end;
            momap_del1(j->runs, b);
            b = nb;
        }
        *(mj_run *)momap_put1(j->runs, a, NULL) = (mj_run){b, op->lease, op->member};
    }
    mj_ranges_free(&fresh);
    fold(j);
}

static void apply_fold(mj_job *j, const mj_op *op) {
    if (op->upto <= j->fold_end) return;
    uint64_t first;
    if (!mj_ranges_first(&j->spec.items, &first)) return;
    /* Its runs below `upto` are in the tally now (and the items they did not cover are done there). */
    momap *keep = momap_split_off(j->runs, &op->upto);
    uint64_t a;
    for (const mj_run *r = momap_first(j->runs, &a); r; r = momap_next(j->runs, &a, &a)) {
        uint64_t *c = momap_get1(j->by_member, r->member);
        if (c) *c = *c > r->end - a ? *c - (r->end - a) : 0;
    }
    momap_free(j->runs);
    j->runs = keep;
    uint64_t m;
    for (const uint64_t *n = momap_first(j->folded, &m); n; n = momap_next(j->folded, &m, &m)) {
        uint64_t *c = momap_get1(j->by_member, m);
        if (c) *c = *c > *n ? *c - *n : 0;
    }
    momap_clear(j->folded);
    for (size_t i = 0; i < op->ntally; i++) *(uint64_t *)momap_put1(j->folded, op->tally[i].member, NULL) = op->tally[i].n;
    for (const uint64_t *n = momap_first(j->folded, &m); n; n = momap_next(j->folded, &m, &m)) count_add(j->by_member, (uint32_t)m, *n);
    mj_ranges span = mj_ranges_of(first, op->upto);
    mj_ranges r = mj_ranges_intersect(&span, &j->spec.items);
    mj_ranges_free(&span);
    uint64_t x;
    for (const void *v = momap_first(j->failed, &x); v; v = momap_next(j->failed, &x, &x)) mj_ranges_take(&r, x);
    for (const void *v = momap_first(j->results, &x); v; v = momap_next(j->results, &x, &x)) mj_ranges_take(&r, x);
    mj_ranges_union(&j->done, &r);
    mj_ranges_free(&r);
    j->fold_end = op->upto;
    fold(j);
}

void mj_table_apply(mj_table *t, const mj_op *op) {
    if (op->kind == MJ_OP_FORGET) {
        momap_del1(t->jobs, op->job);
        if (!mj_table_forgot(t, op->job)) {
            if (t->nforgotten == t->capforgotten) {
                t->capforgotten = t->capforgotten ? 2 * t->capforgotten : 4;
                t->forgotten = mxrealloc(t->forgotten, t->capforgotten * sizeof *t->forgotten);
            }
            t->forgotten[t->nforgotten++] = op->job;
        }
        return;
    }
    if (op->kind == MJ_OP_SUBMIT) {
        if (!mj_table_forgot(t, op->spec.id)) {
            bool fresh;
            mj_job **p = momap_put1(t->jobs, op->spec.id, &fresh);
            if (fresh) *p = mj_job_new(&op->spec);
        }
        return;
    }
    mj_job *j = mj_table_get(t, mj_op_job(op));
    if (!j) return;
    switch (op->kind) {
    case MJ_OP_CANCEL:
        if (!j->cancel) j->cancel = mstrdup(op->why ? op->why : "");
        break;
    case MJ_OP_DONE:
        if (mj_job_settled(j, op->item) || !mj_ranges_contains(&j->spec.items, op->item)) return;
        mj_ranges_add(&j->done, op->item);
        count_add(j->by_member, op->res.member, 1);
        {
            bool fresh;
            mj_res *r = momap_put1(j->results, op->item, &fresh);
            if (!fresh) mj_res_free(r);
            *r = mj_res_clone(&op->res);
        }
        fold(j);
        break;
    case MJ_OP_DONE_RUN: done_run(j, op); break;
    case MJ_OP_FOLD: apply_fold(j, op); break;
    case MJ_OP_FAIL:
        if (mj_job_settled(j, op->item)) return;
        *(mj_failed *)momap_put1(j->failed, op->item, NULL) = (mj_failed){op->tries, mstrdup(op->why ? op->why : "")};
        break;
    case MJ_OP_FAILS: {
        uint32_t *t2 = momap_put1(j->fails, op->item, NULL);
        if (op->tries > *t2) *t2 = op->tries;
        break;
    }
    case MJ_OP_DUPES:
        if (op->n > j->dupes) j->dupes = op->n;
        break;
    case MJ_OP_FINISH:
        if (!j->finished) {
            j->finished = true;
            j->finish_at = op->at_ms;
            j->finish_state = op->state;
        }
        break;
    case MJ_OP_TONE:
        if (op->seq > j->tone_seq) {
            j->tone_seq = op->seq;
            j->tone = op->tone;
        }
        break;
    default: break;
    }
}

void mj_table_ops(const mj_table *t, mj_ops *out) {
    uint64_t id;
    for (const mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id)) {
        mj_job_id job = j->spec.id;
        mj_ops_push(out, mj_op_submit(&j->spec));
        uint64_t k;
        for (const mj_res *r = momap_first(j->results, &k); r; r = momap_next(j->results, &k, &k)) mj_ops_push(out, mj_op_done(job, k, r));
        for (const mj_failed *f = momap_first(j->failed, &k); f; f = momap_next(j->failed, &k, &k))
            mj_ops_push(out, mj_op_fail(job, k, f->tries, f->why));
        /* After the results and failures (the fold's range is done but for those), before the runs above it. */
        if (j->fold_end > 0) {
            size_t n = momap_len(j->folded), i = 0;
            mj_tally *tl = n ? mxalloc(n * sizeof *tl) : NULL;
            for (const uint64_t *c = momap_first(j->folded, &k); c; c = momap_next(j->folded, &k, &k)) tl[i++] = (mj_tally){(uint32_t)k, *c};
            mj_op o = {.kind = MJ_OP_FOLD, .job = job, .upto = j->fold_end, .tally = tl, .ntally = n};
            mj_ops_push(out, o);
        }
        for (const mj_run *r = momap_first(j->runs, &k); r; r = momap_next(j->runs, &k, &k))
            mj_ops_push(out, mj_op_done_run(job, k, r->end, r->lease, r->member));
        for (const uint32_t *tr = momap_first(j->fails, &k); tr; tr = momap_next(j->fails, &k, &k)) mj_ops_push(out, mj_op_fails(job, k, *tr));
        if (j->dupes > 0) mj_ops_push(out, mj_op_dupes(job, j->dupes));
        if (j->cancel) mj_ops_push(out, mj_op_cancel(job, j->cancel));
        if (j->finished) mj_ops_push(out, mj_op_finish(job, j->finish_at, j->finish_state));
        if (j->tone_seq > 0) mj_ops_push(out, mj_op_tone(job, j->tone_seq, j->tone));
    }
    for (size_t i = 0; i < t->nforgotten; i++) mj_ops_push(out, mj_op_forget(t->forgotten[i]));
}

mj_job_id mj_table_next_id(const mj_table *t, uint64_t term) {
    uint64_t n = 0, id;
    for (const mj_job *j = mj_table_first(t, &id); j; j = mj_table_next(t, &id))
        if (id >> 32 == term && (id & 0xffffffffu) > n) n = id & 0xffffffffu;
    for (size_t i = 0; i < t->nforgotten; i++)
        if (t->forgotten[i] >> 32 == term && (t->forgotten[i] & 0xffffffffu) > n) n = t->forgotten[i] & 0xffffffffu;
    return term << 32 | (n + 1);
}
