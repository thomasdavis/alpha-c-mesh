/* jobs.rs's Log (encode, decode), Entry, Replica (apply, last, log) and replay. */
#include "mesh/jobs/log.h"

#include <stdlib.h>
#include <string.h>

void mj_log_free(mj_log *l) {
    mj_ops_free(&l->ops);
    memset(l, 0, sizeof *l);
}

mj_log mj_log_clone(const mj_log *l) {
    mj_log c = *l;
    c.ops = mj_ops_clone(&l->ops);
    return c;
}

void mj_log_encode(const mj_log *l, msb *out) {
    msb w = {0};
    switch (l->kind) {
    case MJ_LOG_LID: mj_w_u64(&w, l->lid); break;
    case MJ_LOG_SNAPSHOT:
        mj_w_u64(&w, l->lid);
        mj_w_u64(&w, l->n);
        mj_encode_ops(l->ops.v, l->ops.n, &w);
        break;
    case MJ_LOG_ENTRY:
        mj_w_u64(&w, l->lid);
        mj_w_u64(&w, l->n);
        mj_w_u8(&w, l->replace ? 1 : 0);
        mj_encode_ops(l->ops.v, l->ops.n, &w);
        break;
    case MJ_LOG_COMMIT:
        mj_w_u64(&w, l->lid);
        mj_w_u64(&w, l->n);
        break;
    }
    mj_w_u8(out, (uint8_t)l->kind);
    mj_w_u64(out, w.n);
    msb_put(out, w.p, w.n);
    msb_free(&w);
}

bool mj_log_decode(uint8_t kind, const void *body, size_t n, mj_log *out, char **err) {
    mj_rd r = mj_rd_new(body, n);
    mj_log l = {.kind = (mj_log_kind)kind};
    bool ok;
    switch (kind) {
    case MJ_LOG_LID: ok = mj_rd_u64(&r, &l.lid); break;
    case MJ_LOG_SNAPSHOT: ok = mj_rd_u64(&r, &l.lid) && mj_rd_u64(&r, &l.n) && mj_rd_ops(&r, &l.ops); break;
    case MJ_LOG_ENTRY: {
        uint8_t rep;
        ok = mj_rd_u64(&r, &l.lid) && mj_rd_u64(&r, &l.n) && mj_rd_u8(&r, &rep);
        if (ok) l.replace = rep != 0;
        ok = ok && mj_rd_ops(&r, &l.ops);
        break;
    }
    case MJ_LOG_COMMIT: ok = mj_rd_u64(&r, &l.lid) && mj_rd_u64(&r, &l.n); break;
    default: ok = mj_rd_fail(&r, mfmt("job log: record kind %u?", kind));
    }
    if (ok && !mj_rd_done(&r)) ok = mj_rd_fail(&r, mfmt("job log: record kind %u: %zu trailing bytes", kind, n - r.at));
    if (!ok) {
        mj_log_free(&l);
        if (err) *err = mj_rd_take_err(&r);
        mj_rd_free(&r);
        return false;
    }
    *out = l;
    return true;
}

void mj_entry_free(mj_entry *e) {
    mj_ops_free(&e->ops);
    if (e->replace) mj_table_free(&e->table);
    memset(e, 0, sizeof *e);
}

mj_entry mj_entry_clone(const mj_entry *e) {
    mj_entry c = *e;
    c.ops = mj_ops_clone(&e->ops);
    if (e->replace) c.table = mj_table_clone(&e->table);
    return c;
}

bool mj_entry_eq(const mj_entry *a, const mj_entry *b) {
    if (a->lid != b->lid || a->n != b->n || a->replace != b->replace) return false;
    return a->replace ? mj_table_eq(&a->table, &b->table) : mj_ops_eq(&a->ops, &b->ops);
}

void mj_replica_init(mj_replica *r) {
    memset(r, 0, sizeof *r);
    mj_table_init(&r->committed);
}

void mj_replica_free(mj_replica *r) {
    mj_table_free(&r->committed);
    for (size_t i = 0; i < r->npending; i++) mj_entry_free(&r->pending[i]);
    free(r->pending);
    memset(r, 0, sizeof *r);
}

mj_replica mj_replica_clone(const mj_replica *r) {
    mj_replica c = *r;
    c.committed = mj_table_clone(&r->committed);
    c.pending = r->npending ? mxalloc(r->npending * sizeof *c.pending) : NULL;
    for (size_t i = 0; i < r->npending; i++) c.pending[i] = mj_entry_clone(&r->pending[i]);
    c.cappending = r->npending;
    return c;
}

bool mj_replica_eq(const mj_replica *a, const mj_replica *b) {
    if (a->lid != b->lid || a->commit_lid != b->commit_lid || a->commit_n != b->commit_n || !mj_table_eq(&a->committed, &b->committed) ||
        a->npending != b->npending)
        return false;
    for (size_t i = 0; i < a->npending; i++)
        if (!mj_entry_eq(&a->pending[i], &b->pending[i])) return false;
    return true;
}

static void push_pending(mj_replica *r, mj_entry e) {
    if (r->npending == r->cappending) {
        r->cappending = r->cappending ? 2 * r->cappending : 8;
        r->pending = mxrealloc(r->pending, r->cappending * sizeof *r->pending);
    }
    r->pending[r->npending++] = e;
}

static mj_table table_of(const mj_ops *ops) {
    mj_table t;
    mj_table_init(&t);
    for (size_t i = 0; i < ops->n; i++) mj_table_apply(&t, &ops->v[i]);
    return t;
}

void mj_replica_apply(mj_replica *r, const mj_log *l) {
    switch (l->kind) {
    case MJ_LOG_LID:
        if (l->lid > r->lid) r->lid = l->lid;
        break;
    case MJ_LOG_SNAPSHOT: {
        mj_table t = table_of(&l->ops);
        mj_table_free(&r->committed);
        r->committed = t;
        r->commit_lid = l->lid;
        r->commit_n = l->n;
        /* What the snapshot does not cover stays (review finding 6): that leadership's entries past it,
         * in sequence; later leaderships'; and earlier ones' while that leadership has committed
         * nothing (n = 0: its first entry, the merge, would cover them). */
        uint64_t next = l->n + 1;
        size_t k = 0;
        for (size_t i = 0; i < r->npending; i++) {
            mj_entry *e = &r->pending[i];
            bool keep;
            if (e->lid < l->lid) keep = l->n == 0;
            else if (e->lid > l->lid) keep = true;
            else if (e->n == next) {
                next++;
                keep = true;
            } else keep = false;
            if (keep) r->pending[k++] = *e;
            else mj_entry_free(e);
        }
        r->npending = k;
        break;
    }
    case MJ_LOG_ENTRY: {
        mj_entry e = {.lid = l->lid, .n = l->n, .replace = l->replace};
        if (l->replace) {
            /* A new leadership's merge: it holds every entry of the leaderships before it a majority had. */
            size_t k = 0;
            for (size_t i = 0; i < r->npending; i++) {
                if (r->pending[i].lid == l->lid) r->pending[k++] = r->pending[i];
                else mj_entry_free(&r->pending[i]);
            }
            r->npending = k;
            e.table = table_of(&l->ops);
        } else e.ops = mj_ops_clone(&l->ops);
        push_pending(r, e);
        break;
    }
    case MJ_LOG_COMMIT: {
        size_t i = 0;
        while (i < r->npending && r->pending[i].lid == l->lid && r->pending[i].n <= l->n) {
            mj_entry *e = &r->pending[i];
            if (e->replace) {
                mj_table_free(&r->committed);
                r->committed = e->table;
                e->replace = false; /* moved */
            } else
                for (size_t k = 0; k < e->ops.n; k++) mj_table_apply(&r->committed, &e->ops.v[k]);
            r->commit_lid = l->lid;
            r->commit_n = e->n;
            mj_entry_free(e);
            i++;
        }
        if (i) {
            memmove(r->pending, r->pending + i, (r->npending - i) * sizeof *r->pending);
            r->npending -= i;
        }
        break;
    }
    }
}

uint64_t mj_replica_last(const mj_replica *r, uint64_t lid) {
    for (size_t i = r->npending; i-- > 0;)
        if (r->pending[i].lid == lid) return r->pending[i].n;
    return 0;
}

void mj_logs_push(mj_logs *v, mj_log l) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 8;
        v->v = mxrealloc(v->v, v->cap * sizeof *v->v);
    }
    v->v[v->n++] = l;
}

void mj_logs_free(mj_logs *v) {
    for (size_t i = 0; i < v->n; i++) mj_log_free(&v->v[i]);
    free(v->v);
    *v = (mj_logs){0};
}

mj_logs mj_replica_log(const mj_replica *r) {
    mj_logs v = {0};
    mj_logs_push(&v, mj_log_lid(r->lid));
    mj_ops snap = {0};
    mj_table_ops(&r->committed, &snap);
    mj_logs_push(&v, mj_log_snapshot(r->commit_lid, r->commit_n, snap));
    for (size_t i = 0; i < r->npending; i++) {
        const mj_entry *e = &r->pending[i];
        mj_ops ops = {0};
        if (e->replace) mj_table_ops(&e->table, &ops);
        else ops = mj_ops_clone(&e->ops);
        mj_logs_push(&v, mj_log_entry(e->lid, e->n, e->replace, ops));
    }
    return v;
}

bool mj_replay(const void *bytes, size_t n, mj_replica *out, size_t *records, size_t *whole, char **err) {
    const uint8_t *b = bytes;
    mj_replica rep;
    mj_replica_init(&rep);
    size_t at = 0, k = 0;
    while (n - at >= 9) {
        uint64_t len = 0;
        for (int i = 8; i >= 1; i--) len = len << 8 | b[at + (size_t)i];
        if ((uint64_t)(n - at - 9) < len) break;
        mj_log l;
        char *e = NULL;
        if (!mj_log_decode(b[at], b + at + 9, (size_t)len, &l, &e)) {
            if (err) *err = mfmt("%s (record %zu at byte %zu)", e, k, at);
            free(e);
            mj_replica_free(&rep);
            return false;
        }
        mj_replica_apply(&rep, &l);
        mj_log_free(&l);
        at += 9 + (size_t)len;
        k++;
    }
    *out = rep;
    if (records) *records = k;
    if (whole) *whole = at;
    return true;
}
