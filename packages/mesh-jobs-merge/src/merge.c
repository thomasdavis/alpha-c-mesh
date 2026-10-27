/* jobs.rs's Answer and merge. */
#include "mesh/jobs/merge.h"

#include <stdlib.h>
#include <string.h>

static void ops_free(void *v) { mj_ops_free(v); }
static void ops_copy(void *d, const void *s) { *(mj_ops *)d = mj_ops_clone(s); }

void mj_answer_init(mj_answer *a) {
    memset(a, 0, sizeof *a);
    mj_table_init(&a->committed);
    a->entries = momap_new(2, sizeof(mj_ops), ops_free, ops_copy);
}

void mj_answer_free(mj_answer *a) {
    mj_table_free(&a->committed);
    momap_free(a->entries);
    memset(a, 0, sizeof *a);
}

mj_ops *mj_answer_entry(mj_answer *a, uint64_t lid, uint64_t n) {
    uint64_t k[2] = {lid, n};
    return momap_put(a->entries, k, NULL);
}

mj_answer mj_answer_of(const mj_replica *r) {
    mj_answer a;
    mj_answer_init(&a);
    a.commit_lid = r->commit_lid;
    a.commit_n = r->commit_n;
    mj_table_free(&a.committed);
    a.committed = mj_table_clone(&r->committed);
    for (size_t i = 0; i < r->npending; i++) {
        const mj_entry *e = &r->pending[i];
        mj_ops ops = {0};
        if (e->replace) mj_table_ops(&e->table, &ops);
        else ops = mj_ops_clone(&e->ops);
        mj_ops *slot = mj_answer_entry(&a, e->lid, e->n);
        mj_ops_free(slot); /* a later entry under the same key replaces it, as BTreeMap::insert does */
        *slot = ops;
    }
    return a;
}

mj_table mj_merge(mj_answer *const *answers, size_t n) {
    mj_table t;
    mj_table_init(&t);
    /* The committed tables, the latest commit point first (a stable sort: ties keep member order). */
    mj_answer **by = n ? mxalloc(n * sizeof *by) : NULL;
    memcpy(by, answers, n * sizeof *by);
    for (size_t i = 1; i < n; i++) {
        mj_answer *x = by[i];
        size_t j = i;
        while (j > 0 && (by[j - 1]->commit_lid < x->commit_lid || (by[j - 1]->commit_lid == x->commit_lid && by[j - 1]->commit_n < x->commit_n))) {
            by[j] = by[j - 1];
            j--;
        }
        by[j] = x;
    }
    for (size_t i = 0; i < n; i++) {
        mj_ops ops = {0};
        mj_table_ops(&by[i]->committed, &ops);
        for (size_t k = 0; k < ops.n; k++) mj_table_apply(&t, &ops.v[k]);
        mj_ops_free(&ops);
    }
    free(by);
    /* The entries not committed yet, keyed (latest leadership first, then number); the first answer's wins. */
    momap *entries = momap_new(2, sizeof(const mj_ops *), NULL, NULL);
    for (size_t i = 0; i < n; i++) {
        uint64_t k[2];
        for (const mj_ops *ops = momap_first(answers[i]->entries, k); ops; ops = momap_next(answers[i]->entries, k, k)) {
            uint64_t key[2] = {UINT64_MAX - k[0], k[1]};
            bool fresh;
            const mj_ops **slot = momap_put(entries, key, &fresh);
            if (fresh) *slot = ops;
        }
    }
    uint64_t k[2];
    for (const mj_ops **ops = momap_first(entries, k); ops; ops = momap_next(entries, k, k))
        for (size_t j = 0; j < (*ops)->n; j++) mj_table_apply(&t, &(*ops)->v[j]);
    momap_free(entries);
    return t;
}
