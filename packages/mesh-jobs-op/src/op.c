/* jobs.rs's Res, BlobRef, Op, and their encoding (put_op, get_op, encode_ops, decode_ops, parts). */
#include "mesh/jobs/op.h"
#include "mesh/abi/base.h"

#include <stdlib.h>
#include <string.h>

void mj_res_free(mj_res *r) {
    free(r->out);
    r->out = NULL;
    r->out_len = 0;
}

static uint8_t *dup_bytes(const uint8_t *b, size_t n) {
    if (!n) return NULL;
    uint8_t *c = mxalloc(n);
    memcpy(c, b, n);
    return c;
}

mj_res mj_res_clone(const mj_res *r) {
    mj_res c = *r;
    c.out = dup_bytes(r->out, r->out_len);
    return c;
}

bool mj_blob_ref_eq(const mj_blob_ref *a, const mj_blob_ref *b) {
    return mj_hash_eq(&a->hash, &b->hash) && a->len == b->len && a->holders == b->holders;
}

bool mj_res_eq(const mj_res *a, const mj_res *b) {
    return a->lease == b->lease && a->member == b->member && a->result == b->result && a->out_len == b->out_len &&
           (a->out_len == 0 || memcmp(a->out, b->out, a->out_len) == 0) && a->has_blob == b->has_blob &&
           (!a->has_blob || mj_blob_ref_eq(&a->blob, &b->blob));
}

void mj_op_free(mj_op *o) {
    if (o->kind == MJ_OP_SUBMIT) mj_spec_free(&o->spec);
    free(o->why);
    mj_res_free(&o->res);
    free(o->tally);
    memset(o, 0, sizeof *o);
}

mj_op mj_op_clone(const mj_op *o) {
    mj_op c = *o;
    if (o->kind == MJ_OP_SUBMIT) c.spec = mj_spec_clone(&o->spec);
    c.why = o->why ? mstrdup(o->why) : NULL;
    c.res = mj_res_clone(&o->res);
    if (o->ntally) {
        c.tally = mxalloc(o->ntally * sizeof *c.tally);
        memcpy(c.tally, o->tally, o->ntally * sizeof *c.tally);
    }
    return c;
}

static bool str_eq(const char *a, const char *b) { return strcmp(a ? a : "", b ? b : "") == 0; }

bool mj_op_eq(const mj_op *a, const mj_op *b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
    case MJ_OP_SUBMIT: return mj_spec_eq(&a->spec, &b->spec);
    case MJ_OP_CANCEL: return a->job == b->job && str_eq(a->why, b->why);
    case MJ_OP_DONE: return a->job == b->job && a->item == b->item && mj_res_eq(&a->res, &b->res);
    case MJ_OP_DONE_RUN: return a->job == b->job && a->s == b->s && a->e == b->e && a->lease == b->lease && a->member == b->member;
    case MJ_OP_FAIL: return a->job == b->job && a->item == b->item && a->tries == b->tries && str_eq(a->why, b->why);
    case MJ_OP_FAILS: return a->job == b->job && a->item == b->item && a->tries == b->tries;
    case MJ_OP_DUPES: return a->job == b->job && a->n == b->n;
    case MJ_OP_FINISH: return a->job == b->job && a->at_ms == b->at_ms && a->state == b->state;
    case MJ_OP_FORGET: return a->job == b->job;
    case MJ_OP_FOLD:
        if (a->job != b->job || a->upto != b->upto || a->ntally != b->ntally) return false;
        for (size_t i = 0; i < a->ntally; i++)
            if (a->tally[i].member != b->tally[i].member || a->tally[i].n != b->tally[i].n) return false;
        return true;
    case MJ_OP_TONE: return a->job == b->job && a->seq == b->seq && a->tone == b->tone;
    }
    return false;
}

mj_op mj_op_submit(const mj_spec *s) { return (mj_op){.kind = MJ_OP_SUBMIT, .job = s->id, .spec = mj_spec_clone(s)}; }
mj_op mj_op_cancel(mj_job_id job, const char *why) { return (mj_op){.kind = MJ_OP_CANCEL, .job = job, .why = mstrdup(why)}; }
mj_op mj_op_done(mj_job_id job, uint64_t item, const mj_res *res) {
    return (mj_op){.kind = MJ_OP_DONE, .job = job, .item = item, .res = mj_res_clone(res)};
}
mj_op mj_op_done_run(mj_job_id job, uint64_t s, uint64_t e, uint64_t lease, uint32_t member) {
    return (mj_op){.kind = MJ_OP_DONE_RUN, .job = job, .s = s, .e = e, .lease = lease, .member = member};
}
mj_op mj_op_fail(mj_job_id job, uint64_t item, uint32_t tries, const char *why) {
    return (mj_op){.kind = MJ_OP_FAIL, .job = job, .item = item, .tries = tries, .why = mstrdup(why)};
}
mj_op mj_op_fails(mj_job_id job, uint64_t item, uint32_t tries) { return (mj_op){.kind = MJ_OP_FAILS, .job = job, .item = item, .tries = tries}; }
mj_op mj_op_dupes(mj_job_id job, uint64_t n) { return (mj_op){.kind = MJ_OP_DUPES, .job = job, .n = n}; }
mj_op mj_op_finish(mj_job_id job, uint64_t at_ms, uint32_t state) { return (mj_op){.kind = MJ_OP_FINISH, .job = job, .at_ms = at_ms, .state = state}; }
mj_op mj_op_forget(mj_job_id job) { return (mj_op){.kind = MJ_OP_FORGET, .job = job}; }
mj_op mj_op_fold(mj_job_id job, uint64_t upto, const mj_tally *tally, size_t n) {
    mj_op o = {.kind = MJ_OP_FOLD, .job = job, .upto = upto, .ntally = n};
    if (n) {
        o.tally = mxalloc(n * sizeof *o.tally);
        memcpy(o.tally, tally, n * sizeof *o.tally);
    }
    return o;
}
mj_op mj_op_tone(mj_job_id job, uint32_t seq, uint32_t tone) { return (mj_op){.kind = MJ_OP_TONE, .job = job, .seq = seq, .tone = tone}; }

void mj_ops_push(mj_ops *v, mj_op o) {
    if (v->n == v->cap) {
        v->cap = v->cap ? 2 * v->cap : 8;
        v->v = mxrealloc(v->v, v->cap * sizeof *v->v);
    }
    v->v[v->n++] = o;
}

void mj_ops_free(mj_ops *v) {
    for (size_t i = 0; i < v->n; i++) mj_op_free(&v->v[i]);
    free(v->v);
    *v = (mj_ops){0};
}

mj_ops mj_ops_clone(const mj_ops *v) {
    mj_ops c = {0};
    for (size_t i = 0; i < v->n; i++) mj_ops_push(&c, mj_op_clone(&v->v[i]));
    return c;
}

bool mj_ops_eq(const mj_ops *a, const mj_ops *b) {
    if (a->n != b->n) return false;
    for (size_t i = 0; i < a->n; i++)
        if (!mj_op_eq(&a->v[i], &b->v[i])) return false;
    return true;
}

void mj_ops_append(mj_ops *dst, mj_ops *src) {
    for (size_t i = 0; i < src->n; i++) mj_ops_push(dst, src->v[i]);
    free(src->v);
    *src = (mj_ops){0};
}

void mj_w_blob(msb *w, bool has, const mj_blob_ref *b) {
    if (!has) {
        mj_w_u8(w, 0);
        return;
    }
    mj_w_u8(w, 1);
    msb_put(w, b->hash.b, 32);
    mj_w_u32(w, b->len);
    mj_w_u32(w, b->holders);
}

bool mj_rd_blob(mj_rd *r, bool *has, mj_blob_ref *b) {
    uint8_t t;
    if (!mj_rd_u8(r, &t)) return false;
    if (t == 0) {
        *has = false;
        memset(b, 0, sizeof *b);
        return true;
    }
    if (t != 1) return mj_rd_fail(r, mfmt("job frame: blob tag %u?", t));
    const uint8_t *p;
    if (!mj_rd_take(r, 32, &p)) return false;
    memcpy(b->hash.b, p, 32);
    *has = true;
    return mj_rd_u32(r, &b->len) && mj_rd_u32(r, &b->holders);
}

void mj_put_op(msb *w, const mj_op *o) {
    mj_w_u8(w, (uint8_t)o->kind);
    switch (o->kind) {
    case MJ_OP_SUBMIT: {
        msb s = {0};
        mj_spec_encode(&o->spec, &s);
        mj_w_bytes(w, s.p, s.n);
        msb_free(&s);
        break;
    }
    case MJ_OP_CANCEL: mj_w_u64(w, o->job); mj_w_str(w, o->why ? o->why : ""); break;
    case MJ_OP_DONE:
        mj_w_u64(w, o->job);
        mj_w_u64(w, o->item);
        mj_w_u64(w, o->res.lease);
        mj_w_u32(w, o->res.member);
        mj_w_u32(w, o->res.result);
        mj_w_bytes(w, o->res.out, o->res.out_len);
        mj_w_blob(w, o->res.has_blob, &o->res.blob);
        break;
    case MJ_OP_DONE_RUN:
        mj_w_u64(w, o->job);
        mj_w_u64(w, o->s);
        mj_w_u64(w, o->e);
        mj_w_u64(w, o->lease);
        mj_w_u32(w, o->member);
        break;
    case MJ_OP_FAIL:
        mj_w_u64(w, o->job);
        mj_w_u64(w, o->item);
        mj_w_u32(w, o->tries);
        mj_w_str(w, o->why ? o->why : "");
        break;
    case MJ_OP_FAILS: mj_w_u64(w, o->job); mj_w_u64(w, o->item); mj_w_u32(w, o->tries); break;
    case MJ_OP_DUPES: mj_w_u64(w, o->job); mj_w_u64(w, o->n); break;
    case MJ_OP_FINISH: mj_w_u64(w, o->job); mj_w_u64(w, o->at_ms); mj_w_u32(w, o->state); break;
    case MJ_OP_FORGET: mj_w_u64(w, o->job); break;
    case MJ_OP_FOLD:
        mj_w_u64(w, o->job);
        mj_w_u64(w, o->upto);
        mj_w_u32(w, (uint32_t)o->ntally);
        for (size_t i = 0; i < o->ntally; i++) {
            mj_w_u32(w, o->tally[i].member);
            mj_w_u64(w, o->tally[i].n);
        }
        break;
    case MJ_OP_TONE: mj_w_u64(w, o->job); mj_w_u32(w, o->seq); mj_w_u32(w, o->tone); break;
    }
}

bool mj_get_op(mj_rd *r, mj_op *out) {
    uint8_t t;
    if (!mj_rd_u8(r, &t)) return false;
    mj_op o = {.kind = (mj_op_kind)t};
    bool ok;
    switch (t) {
    case MJ_OP_SUBMIT: {
        char *text = NULL, *err = NULL;
        if (!mj_rd_str(r, &text)) return false;
        ok = mj_spec_decode(text, &o.spec, &err);
        free(text);
        if (!ok) return mj_rd_fail(r, err);
        o.job = o.spec.id;
        break;
    }
    case MJ_OP_CANCEL: ok = mj_rd_u64(r, &o.job) && mj_rd_str(r, &o.why); break;
    case MJ_OP_DONE:
        ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.item) && mj_rd_u64(r, &o.res.lease) && mj_rd_u32(r, &o.res.member) &&
             mj_rd_u32(r, &o.res.result) && mj_rd_bytes(r, &o.res.out, &o.res.out_len) && mj_rd_blob(r, &o.res.has_blob, &o.res.blob);
        if (ok && o.res.out_len == 0) {
            free(o.res.out);
            o.res.out = NULL;
        }
        break;
    case MJ_OP_DONE_RUN:
        ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.s) && mj_rd_u64(r, &o.e) && mj_rd_u64(r, &o.lease) && mj_rd_u32(r, &o.member);
        break;
    case MJ_OP_FAIL: ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.item) && mj_rd_u32(r, &o.tries) && mj_rd_str(r, &o.why); break;
    case MJ_OP_FAILS: ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.item) && mj_rd_u32(r, &o.tries); break;
    case MJ_OP_DUPES: ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.n); break;
    case MJ_OP_FINISH: ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.at_ms) && mj_rd_u32(r, &o.state); break;
    case MJ_OP_FORGET: ok = mj_rd_u64(r, &o.job); break;
    case MJ_OP_FOLD: {
        uint32_t n;
        ok = mj_rd_u64(r, &o.job) && mj_rd_u64(r, &o.upto) && mj_rd_u32(r, &n);
        if (!ok) break;
        if (n > MESH_MAX_SYSTEMS) return mj_rd_fail(r, mfmt("job op fold: %u members", n));
        o.tally = n ? mxalloc(n * sizeof *o.tally) : NULL;
        o.ntally = n;
        for (uint32_t i = 0; ok && i < n; i++) ok = mj_rd_u32(r, &o.tally[i].member) && mj_rd_u64(r, &o.tally[i].n);
        break;
    }
    case MJ_OP_TONE: ok = mj_rd_u64(r, &o.job) && mj_rd_u32(r, &o.seq) && mj_rd_u32(r, &o.tone); break;
    default: return mj_rd_fail(r, mfmt("job op %u?", t));
    }
    if (!ok) {
        mj_op_free(&o);
        return false;
    }
    *out = o;
    return true;
}

void mj_encode_ops(const mj_op *ops, size_t n, msb *out) {
    for (size_t i = 0; i < n; i++) mj_put_op(out, &ops[i]);
}

bool mj_rd_ops(mj_rd *r, mj_ops *out) {
    while (!mj_rd_done(r)) {
        mj_op o;
        if (!mj_get_op(r, &o)) return false;
        mj_ops_push(out, o);
    }
    return true;
}

bool mj_decode_ops(const void *b, size_t n, mj_ops *out, char **err) {
    mj_rd r = mj_rd_new(b, n);
    mj_ops v = {0};
    if (!mj_rd_ops(&r, &v)) {
        mj_ops_free(&v);
        if (err) *err = mj_rd_take_err(&r);
        mj_rd_free(&r);
        return false;
    }
    *out = v;
    return true;
}

mj_parts mj_parts_of(const mj_op *ops, size_t n) {
    mj_parts p = {0};
    msb w = {0};
    for (size_t i = 0; i < n; i++) {
        mj_put_op(&w, &ops[i]);
        if (w.n >= MESH_JOBS_PART_BYTES) {
            p.v = mxrealloc(p.v, (p.n + 1) * sizeof *p.v);
            p.v[p.n++] = w;
            w = (msb){0};
        }
    }
    if (w.n || p.n == 0) {
        p.v = mxrealloc(p.v, (p.n + 1) * sizeof *p.v);
        p.v[p.n++] = w;
    } else msb_free(&w);
    return p;
}

void mj_parts_free(mj_parts *p) {
    for (size_t i = 0; i < p->n; i++) msb_free(&p->v[i]);
    free(p->v);
    *p = (mj_parts){0};
}
