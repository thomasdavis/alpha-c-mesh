/* jobs.rs's Prog, Held, LeaseMsg, ItemRes and Frame (encode, decode). */
#include "mesh/jobs/frame.h"
#include "mesh/abi/peer_frames.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *dup_bytes(const uint8_t *b, size_t n) {
    if (!n) return NULL;
    uint8_t *c = mxalloc(n);
    memcpy(c, b, n);
    return c;
}

void mj_prog_free(mj_prog *p) {
    free(p->name);
    p->name = NULL;
}
mj_prog mj_prog_clone(const mj_prog *p) { return (mj_prog){mstrdup(p->name), p->exec, p->cap}; }
bool mj_prog_eq(const mj_prog *a, const mj_prog *b) { return strcmp(a->name, b->name) == 0 && a->exec == b->exec && a->cap == b->cap; }
bool mj_progs_eq(const mj_prog *a, size_t na, const mj_prog *b, size_t nb) {
    if (na != nb) return false;
    for (size_t i = 0; i < na; i++)
        if (!mj_prog_eq(&a[i], &b[i])) return false;
    return true;
}
mj_prog *mj_progs_clone(const mj_prog *p, size_t n) {
    if (!n) return NULL;
    mj_prog *c = mxalloc(n * sizeof *c);
    for (size_t i = 0; i < n; i++) c[i] = mj_prog_clone(&p[i]);
    return c;
}
void mj_progs_free(mj_prog *p, size_t n) {
    for (size_t i = 0; i < n; i++) mj_prog_free(&p[i]);
    free(p);
}

void mj_held_free(mj_held *h) {
    free(h->program);
    mj_ranges_free(&h->items);
    h->program = NULL;
}
mj_held mj_held_clone(const mj_held *h) { return (mj_held){h->lease, h->job, mstrdup(h->program), mj_ranges_clone(&h->items)}; }
bool mj_held_eq(const mj_held *a, const mj_held *b) {
    return a->lease == b->lease && a->job == b->job && strcmp(a->program, b->program) == 0 && mj_ranges_eq(&a->items, &b->items);
}

void mj_lease_msg_free(mj_lease_msg *l) {
    free(l->program);
    free(l->setup);
    mj_ranges_free(&l->items);
    memset(l, 0, sizeof *l);
}
mj_lease_msg mj_lease_msg_clone(const mj_lease_msg *l) {
    mj_lease_msg c = *l;
    c.program = mstrdup(l->program);
    c.setup = mj_setup_clone(l->setup, l->nsetup);
    c.items = mj_ranges_clone(&l->items);
    return c;
}
bool mj_lease_msg_eq(const mj_lease_msg *a, const mj_lease_msg *b) {
    return a->lease == b->lease && a->job == b->job && strcmp(a->program, b->program) == 0 && a->op == b->op && a->nsetup == b->nsetup &&
           (a->nsetup == 0 || memcmp(a->setup, b->setup, a->nsetup * sizeof *a->setup) == 0) && mj_ranges_eq(&a->items, &b->items);
}

void mj_item_res_free(mj_item_res *r) {
    free(r->out);
    r->out = NULL;
    r->out_len = 0;
}
mj_item_res mj_item_res_clone(const mj_item_res *r) {
    mj_item_res c = *r;
    c.out = dup_bytes(r->out, r->out_len);
    return c;
}
bool mj_item_res_eq(const mj_item_res *a, const mj_item_res *b) {
    return a->lease == b->lease && a->job == b->job && a->item == b->item && a->status == b->status && a->result == b->result &&
           a->out_len == b->out_len && (a->out_len == 0 || memcmp(a->out, b->out, a->out_len) == 0) && a->has_blob == b->has_blob &&
           (!a->has_blob || mj_blob_ref_eq(&a->blob, &b->blob));
}

void mj_frame_free(mj_frame *f) {
    mj_progs_free(f->progs, f->nprogs);
    for (size_t i = 0; i < f->nheld; i++) mj_held_free(&f->held[i]);
    free(f->held);
    mj_ops_free(&f->ops);
    if (f->ty == MESH_F_LEASE) mj_lease_msg_free(&f->l);
    for (size_t i = 0; i < f->nitems; i++) mj_item_res_free(&f->items[i]);
    free(f->items);
    memset(f, 0, sizeof *f);
}

mj_frame mj_frame_clone(const mj_frame *f) {
    mj_frame c = *f;
    c.progs = mj_progs_clone(f->progs, f->nprogs);
    c.held = f->nheld ? mxalloc(f->nheld * sizeof *c.held) : NULL;
    for (size_t i = 0; i < f->nheld; i++) c.held[i] = mj_held_clone(&f->held[i]);
    c.ops = mj_ops_clone(&f->ops);
    if (f->ty == MESH_F_LEASE) c.l = mj_lease_msg_clone(&f->l);
    c.items = f->nitems ? mxalloc(f->nitems * sizeof *c.items) : NULL;
    for (size_t i = 0; i < f->nitems; i++) c.items[i] = mj_item_res_clone(&f->items[i]);
    return c;
}

static bool helds_eq(const mj_held *a, size_t na, const mj_held *b, size_t nb) {
    if (na != nb) return false;
    for (size_t i = 0; i < na; i++)
        if (!mj_held_eq(&a[i], &b[i])) return false;
    return true;
}

bool mj_frame_eq(const mj_frame *a, const mj_frame *b) {
    if (a->ty != b->ty || a->lid != b->lid) return false;
    switch (a->ty) {
    case MESH_F_JOB_AVAIL: return mj_progs_eq(a->progs, a->nprogs, b->progs, b->nprogs) && helds_eq(a->held, a->nheld, b->held, b->nheld);
    case MESH_F_JOB_STATE:
        return a->kind == b->kind && a->n == b->n && a->commit == b->commit && a->seq == b->seq && mj_ops_eq(&a->ops, &b->ops);
    case MESH_F_JOB_ACK: return a->n == b->n && a->gap == b->gap;
    case MESH_F_JOB_PULL: return true;
    case MESH_F_JOB_HAVE:
        return a->kind == b->kind && a->seq == b->seq && a->src0 == b->src0 && a->src1 == b->src1 && mj_ops_eq(&a->ops, &b->ops) &&
               helds_eq(a->held, a->nheld, b->held, b->nheld);
    case MESH_F_LEASE: return mj_lease_msg_eq(&a->l, &b->l);
    case MESH_F_LEASE_CUT: return a->lease == b->lease && a->cut == b->cut;
    case MESH_F_RESULT:
        if (a->nitems != b->nitems) return false;
        for (size_t i = 0; i < a->nitems; i++)
            if (!mj_item_res_eq(&a->items[i], &b->items[i])) return false;
        return true;
    }
    return false;
}

mj_frame mj_frame_avail(uint64_t lid, mj_prog *progs, size_t nprogs, mj_held *held, size_t nheld) {
    return (mj_frame){.ty = MESH_F_JOB_AVAIL, .lid = lid, .progs = progs, .nprogs = nprogs, .held = held, .nheld = nheld};
}
mj_frame mj_frame_state(uint64_t lid, uint32_t kind, uint64_t n, uint64_t commit, uint32_t seq, mj_ops ops) {
    return (mj_frame){.ty = MESH_F_JOB_STATE, .lid = lid, .kind = kind, .n = n, .commit = commit, .seq = seq, .ops = ops};
}
mj_frame mj_frame_ack(uint64_t lid, uint64_t n, bool gap) { return (mj_frame){.ty = MESH_F_JOB_ACK, .lid = lid, .n = n, .gap = gap}; }
mj_frame mj_frame_pull(uint64_t lid) { return (mj_frame){.ty = MESH_F_JOB_PULL, .lid = lid}; }
mj_frame mj_frame_have(uint64_t lid, uint32_t kind, uint32_t seq, uint64_t src0, uint64_t src1, mj_ops ops, mj_held *held, size_t nheld) {
    return (mj_frame){.ty = MESH_F_JOB_HAVE, .lid = lid, .kind = kind, .seq = seq, .src0 = src0, .src1 = src1, .ops = ops, .held = held, .nheld = nheld};
}
mj_frame mj_frame_lease(uint64_t lid, mj_lease_msg l) { return (mj_frame){.ty = MESH_F_LEASE, .lid = lid, .l = l}; }
mj_frame mj_frame_cut(uint64_t lid, uint64_t lease, uint64_t cut) { return (mj_frame){.ty = MESH_F_LEASE_CUT, .lid = lid, .lease = lease, .cut = cut}; }
mj_frame mj_frame_result(uint64_t lid, mj_item_res *items, size_t nitems) {
    return (mj_frame){.ty = MESH_F_RESULT, .lid = lid, .items = items, .nitems = nitems};
}

void mj_w_held(msb *w, const mj_held *h, size_t n) {
    mj_w_u32(w, (uint32_t)n);
    for (size_t i = 0; i < n; i++) {
        mj_w_u64(w, h[i].lease);
        mj_w_u64(w, h[i].job);
        mj_w_str(w, h[i].program);
        mj_w_ranges(w, &h[i].items);
    }
}

bool mj_rd_held(mj_rd *r, mj_held **out, size_t *count) {
    uint32_t n;
    if (!mj_rd_u32(r, &n)) return false;
    mj_held *v = NULL;
    size_t k = 0;
    for (uint32_t i = 0; i < n; i++) {
        mj_held h = {0};
        if (!mj_rd_u64(r, &h.lease) || !mj_rd_u64(r, &h.job) || !mj_rd_str(r, &h.program) || !mj_rd_ranges(r, &h.items)) {
            mj_held_free(&h);
            for (size_t j = 0; j < k; j++) mj_held_free(&v[j]);
            free(v);
            return false;
        }
        v = mxrealloc(v, (k + 1) * sizeof *v);
        v[k++] = h;
    }
    *out = v;
    *count = k;
    return true;
}

void mj_frame_encode(const mj_frame *f, msb *out) {
    msb w = {0};
    mj_w_u64(&w, f->lid);
    switch (f->ty) {
    case MESH_F_JOB_AVAIL:
        mj_w_u32(&w, (uint32_t)f->nprogs);
        for (size_t i = 0; i < f->nprogs; i++) {
            mj_w_str(&w, f->progs[i].name);
            mj_w_u32(&w, f->progs[i].exec);
            mj_w_u32(&w, f->progs[i].cap);
        }
        mj_w_held(&w, f->held, f->nheld);
        break;
    case MESH_F_JOB_STATE:
        mj_w_u32(&w, f->kind);
        mj_w_u64(&w, f->n);
        mj_w_u64(&w, f->commit);
        mj_w_u32(&w, f->seq);
        mj_encode_ops(f->ops.v, f->ops.n, &w);
        break;
    case MESH_F_JOB_ACK:
        mj_w_u64(&w, f->n);
        mj_w_u32(&w, f->gap ? 1 : 0);
        break;
    case MESH_F_JOB_PULL: break;
    case MESH_F_JOB_HAVE:
        mj_w_u32(&w, f->kind);
        mj_w_u32(&w, f->seq);
        mj_w_u64(&w, f->src0);
        mj_w_u64(&w, f->src1);
        if (f->kind == MESH_JOBS_HAVE_END) mj_w_held(&w, f->held, f->nheld);
        else mj_encode_ops(f->ops.v, f->ops.n, &w);
        break;
    case MESH_F_LEASE:
        mj_w_u64(&w, f->l.lease);
        mj_w_u64(&w, f->l.job);
        mj_w_str(&w, f->l.program);
        mj_w_u32(&w, f->l.op);
        mj_w_u32(&w, (uint32_t)f->l.nsetup);
        for (size_t i = 0; i < f->l.nsetup; i++) {
            mj_w_u32(&w, f->l.setup[i].op);
            mj_w_u32(&w, f->l.setup[i].arg);
        }
        mj_w_ranges(&w, &f->l.items);
        break;
    case MESH_F_LEASE_CUT:
        mj_w_u64(&w, f->lease);
        mj_w_u64(&w, f->cut);
        break;
    case MESH_F_RESULT:
        mj_w_u32(&w, (uint32_t)f->nitems);
        for (size_t i = 0; i < f->nitems; i++) {
            const mj_item_res *x = &f->items[i];
            mj_w_u64(&w, x->lease);
            mj_w_u64(&w, x->job);
            mj_w_u64(&w, x->item);
            mj_w_u32(&w, x->status);
            mj_w_u32(&w, x->result);
            mj_w_bytes(&w, x->out, x->out_len);
            mj_w_blob(&w, x->has_blob, &x->blob);
        }
        break;
    }
    mj_w_u8(out, f->ty);
    mj_w_u32(out, (uint32_t)w.n);
    msb_put(out, w.p, w.n);
    msb_free(&w);
}

static bool decode_body(uint8_t ty, mj_rd *r, mj_frame *f) {
    switch (ty) {
    case MESH_F_JOB_AVAIL: {
        uint32_t n;
        if (!mj_rd_u32(r, &n)) return false;
        for (uint32_t i = 0; i < n; i++) {
            mj_prog p = {0};
            if (!mj_rd_str(r, &p.name) || !mj_rd_u32(r, &p.exec) || !mj_rd_u32(r, &p.cap)) {
                mj_prog_free(&p);
                return false;
            }
            f->progs = mxrealloc(f->progs, (f->nprogs + 1) * sizeof *f->progs);
            f->progs[f->nprogs++] = p;
        }
        return mj_rd_held(r, &f->held, &f->nheld);
    }
    case MESH_F_JOB_STATE: return mj_rd_u32(r, &f->kind) && mj_rd_u64(r, &f->n) && mj_rd_u64(r, &f->commit) && mj_rd_u32(r, &f->seq) && mj_rd_ops(r, &f->ops);
    case MESH_F_JOB_ACK: {
        uint32_t g;
        if (!mj_rd_u64(r, &f->n) || !mj_rd_u32(r, &g)) return false;
        f->gap = g != 0;
        return true;
    }
    case MESH_F_JOB_PULL: return true;
    case MESH_F_JOB_HAVE:
        if (!mj_rd_u32(r, &f->kind) || !mj_rd_u32(r, &f->seq) || !mj_rd_u64(r, &f->src0) || !mj_rd_u64(r, &f->src1)) return false;
        if (f->kind < MESH_JOBS_HAVE_COMMITTED || f->kind > MESH_JOBS_HAVE_END) return mj_rd_fail(r, mfmt("F_JOB_HAVE kind %u?", f->kind));
        if (f->kind == MESH_JOBS_HAVE_END) return mj_rd_held(r, &f->held, &f->nheld);
        return mj_rd_ops(r, &f->ops);
    case MESH_F_LEASE: {
        uint32_t ns;
        if (!mj_rd_u64(r, &f->l.lease) || !mj_rd_u64(r, &f->l.job) || !mj_rd_str(r, &f->l.program) || !mj_rd_u32(r, &f->l.op) ||
            !mj_rd_u32(r, &ns))
            return false;
        for (uint32_t i = 0; i < ns; i++) {
            mj_setup s;
            if (!mj_rd_u32(r, &s.op) || !mj_rd_u32(r, &s.arg)) return false;
            f->l.setup = mxrealloc(f->l.setup, (f->l.nsetup + 1) * sizeof *f->l.setup);
            f->l.setup[f->l.nsetup++] = s;
        }
        return mj_rd_ranges(r, &f->l.items);
    }
    case MESH_F_LEASE_CUT: return mj_rd_u64(r, &f->lease) && mj_rd_u64(r, &f->cut);
    case MESH_F_RESULT: {
        uint32_t n;
        if (!mj_rd_u32(r, &n)) return false;
        for (uint32_t i = 0; i < n; i++) {
            mj_item_res x = {0};
            if (!mj_rd_u64(r, &x.lease) || !mj_rd_u64(r, &x.job) || !mj_rd_u64(r, &x.item) || !mj_rd_u32(r, &x.status) ||
                !mj_rd_u32(r, &x.result) || !mj_rd_bytes(r, &x.out, &x.out_len) || !mj_rd_blob(r, &x.has_blob, &x.blob)) {
                mj_item_res_free(&x);
                return false;
            }
            if (!x.out_len) {
                free(x.out);
                x.out = NULL;
            }
            f->items = mxrealloc(f->items, (f->nitems + 1) * sizeof *f->items);
            f->items[f->nitems++] = x;
        }
        return true;
    }
    default: return mj_rd_fail(r, mfmt("job frame type %u?", ty));
    }
}

bool mj_frame_decode(uint8_t ty, const void *body, size_t n, mj_frame *out, char **err) {
    mj_rd r = mj_rd_new(body, n);
    mj_frame f = {.ty = ty};
    bool ok = mj_rd_u64(&r, &f.lid) && decode_body(ty, &r, &f);
    if (ok && !mj_rd_done(&r)) ok = mj_rd_fail(&r, mfmt("job frame type %u: %zu trailing bytes", ty, n - r.at));
    if (!ok) {
        mj_frame_free(&f);
        if (err) *err = mj_rd_take_err(&r);
        mj_rd_free(&r);
        return false;
    }
    *out = f;
    return true;
}
