/* blobd.rs's Blobd: keep, ensure, the events, the frames, news, watch, tick, render, resolve. */
#include "mesh/blobd/blobd.h"
#include "mesh/abi/peer_frames.h"
#include "mesh/blobs/durable.h"
#include "mesh/blobs/frame.h"
#include "mesh/omap.h"
#include "mesh/ref/sha256d.h"
#include "mesh/strbuf.h"
#include "pathtrace/scene.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    mj_job_id *v;
    size_t n;
} jobset;

static void js_add(jobset *s, mj_job_id j) {
    size_t at = 0;
    while (at < s->n && s->v[at] < j) at++;
    if (at < s->n && s->v[at] == j) return;
    s->v = mxrealloc(s->v, (s->n + 1) * sizeof *s->v);
    memmove(&s->v[at + 1], &s->v[at], (s->n - at) * sizeof *s->v);
    s->v[at] = j;
    s->n++;
}

static void js_del(jobset *s, mj_job_id j) {
    for (size_t i = 0; i < s->n; i++)
        if (s->v[i] == j) {
            memmove(&s->v[i], &s->v[i + 1], (s->n - i - 1) * sizeof *s->v);
            s->n--;
            return;
        }
}

typedef struct {
    jobset jobs; /* the jobs that still push it */
    uint8_t *bytes;
    size_t n;
} memblob;

typedef struct {
    mj_job_id job;
    uint64_t item;
} jobitem;

typedef struct {
    mj_job_id job;
    uint32_t holders, tried;
    bool asked;
    uint32_t asked_k;
    int64_t asked_at;
    jobset keep;      /* the jobs whose reference is kept once it arrives (the leader's parked results) */
    jobitem *items;   /* the assemblies waiting for it */
    size_t nitems;
    int64_t after;
    uint32_t absent;  /* members that answered "not here" this round */
    bool lost;        /* a whole round of the members up answered "not here" */
} fetch;

typedef struct {
    masm *a;
    int64_t asked_at;
    char *sha;
} asmx;

typedef struct {
    uint32_t to;
    mj_job_id job;
    mj_hash h;
} pushk;

struct mbd {
    mb_store *store;
    uint32_t me;
    mbd_io io;
    momap *mem;      /* hash -> memblob */
    pushk *pushq;
    size_t npushq, cappushq;
    momap *queued;   /* (to, job, hash) */
    momap *inflight; /* (to, job, hash) -> int64_t sent at */
    momap *owed;     /* (job, hash) -> u32 list: acks owed once a reference being written is durable */
    momap *forgotten;
    uint64_t refused;
    bool full_logged_set;
    int64_t full_logged;
    momap *fetches;  /* hash -> fetch */
    momap *jobs;     /* id -> mbd_job_info */
    momap *asm_;     /* id -> asmx */
    uint64_t puts_in, puts_out, gets_served, bad_in;
};

typedef struct {
    uint32_t *v;
    size_t n;
} u32list;

static void memblob_free(void *v) {
    memblob *m = v;
    free(m->jobs.v);
    free(m->bytes);
}
static void fetch_free(void *v) {
    fetch *f = v;
    free(f->keep.v);
    free(f->items);
}
static void asm_free(void *v) {
    asmx *a = v;
    masm_free(a->a);
    free(a->sha);
}
static void info_free(void *v) {
    mbd_job_info *j = v;
    free(j->program);
    free(j->setup);
    free(j->state);
}
static void u32list_free(void *v) { free(((u32list *)v)->v); }

void mbd_write_free(mbd_write *w) {
    free(w->dir);
    free(w->link);
    free(w->bytes);
    memset(w, 0, sizeof *w);
}

void mbd_news_free(mbd_news *n) {
    for (size_t i = 0; i < n->njobs; i++) info_free(&n->jobs[i]);
    free(n->jobs);
    free(n->forgotten);
    free(n->results);
    memset(n, 0, sizeof *n);
}

static void hk(uint64_t k[4], const mj_hash *h) { mj_hash_words(h, k); }
static void k5(uint64_t k[5], mj_job_id job, const mj_hash *h) {
    k[0] = job;
    mj_hash_words(h, k + 1);
}
static void k6(uint64_t k[6], uint32_t to, mj_job_id job, const mj_hash *h) {
    k[0] = to;
    k[1] = job;
    mj_hash_words(h, k + 2);
}

static void say(mbd *b, bool error, char *line) {
    if (b->io.log) b->io.log(b->io.ctx, error, line);
    free(line);
}

static char *word(mj_job_id id) {
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(id, w);
    return mstrdup(w);
}

static char *hexs(const mj_hash *h) {
    char *x = mxalloc(65);
    mj_hex(h, x);
    return x;
}

static void stored1(mbd *b, mj_job_id job, const mj_hash *h) {
    mb_ref r = {job, *h};
    b->io.stored(b->io.ctx, &r, 1);
}

mbd *mbd_new(uint32_t me, mb_store *store, mbd_io io) {
    mbd *b = mxalloc(sizeof *b);
    b->store = store;
    b->me = me;
    b->io = io;
    b->mem = momap_new(4, sizeof(memblob), memblob_free, NULL);
    b->queued = momap_new(6, 1, NULL, NULL);
    b->inflight = momap_new(6, sizeof(int64_t), NULL, NULL);
    b->owed = momap_new(5, sizeof(u32list), u32list_free, NULL);
    b->forgotten = momap_new(1, 1, NULL, NULL);
    b->fetches = momap_new(4, sizeof(fetch), fetch_free, NULL);
    b->jobs = momap_new(1, sizeof(mbd_job_info), info_free, NULL);
    b->asm_ = momap_new(1, sizeof(asmx), asm_free, NULL);
    mj_job_id *js;
    size_t nj = mb_jobs(store, &js);
    free(js);
    say(b, false, mfmt("blobs: %s (%zu blob(s), %llu reference(s) of %zu job(s), %llu of %llu MiB)",
                       mb_persisted(store) ? "kept on disk" : "kept in memory (no --blobs dir)", mb_count(store), (unsigned long long)mb_refs_total(store),
                       nj, (unsigned long long)(mb_bytes(store) >> 20), (unsigned long long)(mb_cap(store) >> 20)));
    /* The jobs thread learns what is here (the leader commits only what it holds, for that job). */
    mb_refs all = mb_all(store);
    if (all.n) b->io.stored(b->io.ctx, all.v, all.n);
    mb_refs_free(&all);
    return b;
}

void mbd_free(mbd *b) {
    if (!b) return;
    mb_store_free(b->store);
    momap_free(b->mem);
    free(b->pushq);
    momap_free(b->queued);
    momap_free(b->inflight);
    momap_free(b->owed);
    momap_free(b->forgotten);
    momap_free(b->fetches);
    momap_free(b->jobs);
    momap_free(b->asm_);
    free(b);
}

mb_store *mbd_store(mbd *b) { return b->store; }

static bool is_forgotten(const mbd *b, mj_job_id j) { return momap_get1(b->forgotten, j) != NULL; }

static mb_live live(void *ctx, mj_job_id j) { return is_forgotten(ctx, j) ? MB_FORGOTTEN : MB_LISTED; }

static void ack(mbd *b, uint32_t to, mj_job_id job, const mj_hash *h, bool ok) {
    msb f = {0};
    mbf_ack(&f, h, ok, job);
    b->io.send_peer(b->io.ctx, to, (const uint8_t *)f.p, f.n);
    msb_free(&f);
}

/* A present or in-memory blob's bytes (a copy), or NULL. */
static uint8_t *bytes_of(mbd *b, const mj_hash *h, size_t *n) {
    uint64_t k[4];
    hk(k, h);
    memblob *m = momap_get(b->mem, k);
    if (m) {
        uint8_t *c = mxalloc(m->n ? m->n : 1);
        memcpy(c, m->bytes, m->n);
        *n = m->n;
        return c;
    }
    return mb_get(b->store, h, n);
}

static bool here(const mbd *b, const mj_hash *h) {
    uint64_t k[4];
    hk(k, h);
    return momap_get(b->mem, k) != NULL || mb_has(b->store, h);
}

static void written(mbd *b, mj_job_id job, mj_hash hash, mbd_why why, uint32_t from, bool ok, const char *err);

/* Job's reference to hash made (written, or linked to another job's file of the same bytes), or kept in
 * memory without a store dir. Takes bytes. False with *err (refused: full, forgotten). */
static bool keep(mbd *b, mj_job_id job, mj_hash hash, uint8_t *bytes, size_t n, mbd_why why, uint32_t from, char **err) {
    if (is_forgotten(b, job)) {
        char *w = word(job);
        *err = mfmt("job %s is forgotten", w);
        free(w);
        free(bytes);
        return false;
    }
    mb_reserved r;
    char *e = NULL;
    if (!mb_reserve(b->store, job, hash, (uint32_t)n, live, b, &r, &e)) {
        b->refused++;
        int64_t now = b->io.now_ms(b->io.ctx);
        if (!b->full_logged_set || now - b->full_logged > 10000) {
            b->full_logged_set = true;
            b->full_logged = now;
            char *w = word(job);
            say(b, true, mfmt("blobs: %s: %llu cop(ies) refused so far (job %s the latest); this member is no copy for them, and while it leads no payload result is committed",
                              e, (unsigned long long)b->refused, w));
            free(w);
        }
        *err = e;
        free(bytes);
        return false;
    }
    if (r.evicted.n) {
        say(b, false, mfmt("blobs: %zu reference(s) of forgotten jobs evicted for room", r.evicted.n));
        b->io.gone(b->io.ctx, r.evicted.v, r.evicted.n);
    }
    char *d = mb_dir_of(b->store, job);
    if (d) {
        mbd_write w = {d, hash, r.link ? r.path : NULL, bytes, n, why, from, job};
        r.path = NULL;
        b->io.write(b->io.ctx, w);
    } else {
        mb_put_mem(b->store, job, hash, bytes, n);
        free(bytes);
        written(b, job, hash, why, from, true, NULL);
    }
    mb_reserved_free(&r);
    return true;
}

/* Job's copy of hash here: told to jobd if it is, being made if it is, else made from bytes (or the bytes
 * here for another job). Takes bytes (may be NULL). */
static bool ensure(mbd *b, mj_job_id job, mj_hash hash, uint8_t *bytes, size_t n, mbd_why why, char **err) {
    if (mb_has_ref(b->store, job, &hash)) {
        free(bytes);
        stored1(b, job, &hash);
        return true;
    }
    if (mb_known_ref(b->store, job, &hash)) {
        free(bytes);
        return true;
    }
    if (!bytes) bytes = bytes_of(b, &hash, &n);
    if (!bytes) {
        *err = mstrdup("its bytes are not here");
        return false;
    }
    return keep(b, job, hash, bytes, n, why, 0, err);
}

void mbd_local(mbd *b, mj_job_id job, mj_hash hash, uint8_t *bytes, size_t n) {
    uint64_t k[4];
    hk(k, &hash);
    bool fresh;
    memblob *m = momap_put(b->mem, k, &fresh);
    if (fresh) {
        m->bytes = mxalloc(n ? n : 1);
        memcpy(m->bytes, bytes, n);
        m->n = n;
        b->io.mem(b->io.ctx, (int64_t)n);
    }
    js_add(&m->jobs, job);
    char *e = NULL;
    if (!ensure(b, job, hash, bytes, n, MBD_WHY_LOCAL, &e)) {
        char *x = hexs(&hash), *w = word(job);
        say(b, true, mfmt("blobs: own payload %s of job %s: %s", x, w, e));
        free(x);
        free(w);
        free(e);
        b->io.acked(b->io.ctx, b->me, job, &hash, false);
    }
}

void mbd_done(mbd *b, mj_job_id job, mj_hash hash) {
    uint64_t k[4];
    hk(k, &hash);
    memblob *m = momap_get(b->mem, k);
    if (!m) return;
    js_del(&m->jobs, job);
    if (!m->jobs.n) {
        b->io.mem(b->io.ctx, -(int64_t)m->n);
        momap_del(b->mem, k);
    }
}

void mbd_push(mbd *b, uint32_t to, mj_job_id job, mj_hash hash) {
    uint64_t k[6];
    k6(k, to, job, &hash);
    if (to == b->me || momap_get(b->queued, k) || momap_get(b->inflight, k)) return;
    momap_put(b->queued, k, NULL);
    if (b->npushq == b->cappushq) {
        b->cappushq = b->cappushq ? 2 * b->cappushq : 64;
        b->pushq = mxrealloc(b->pushq, b->cappushq * sizeof *b->pushq);
    }
    b->pushq[b->npushq++] = (pushk){to, job, hash};
}

static fetch *fetch_of(mbd *b, const mj_hash *h, mj_job_id job, uint32_t holders) {
    uint64_t k[4];
    hk(k, h);
    bool fresh;
    fetch *f = momap_put(b->fetches, k, &fresh);
    if (fresh) {
        f->job = job;
        f->holders = holders;
        f->after = b->io.now_ms(b->io.ctx);
    }
    return f;
}

void mbd_fetch(mbd *b, mj_job_id job, mj_hash hash, uint32_t holders) {
    if (mb_known_ref(b->store, job, &hash) || here(b, &hash)) {
        char *e = NULL;
        if (!ensure(b, job, hash, NULL, 0, MBD_WHY_FETCHED, &e)) {
            char *x = hexs(&hash), *w = word(job);
            say(b, true, mfmt("blobs: job %s's copy of %s: %s", w, x, e));
            free(x);
            free(w);
            free(e);
        }
        return;
    }
    fetch *f = fetch_of(b, &hash, job, holders);
    js_add(&f->keep, job);
    f->holders |= holders;
}

static void written(mbd *b, mj_job_id job, mj_hash hash, mbd_why why, uint32_t from, bool ok, const char *err) {
    mb_written(b->store, job, &hash, ok);
    uint64_t k[5];
    k5(k, job, &hash);
    u32list owed = {0};
    momap_take(b->owed, k, &owed);
    if (ok && mb_has_ref(b->store, job, &hash)) {
        /* jobd hears of the copy before any member does (the leader's commit check follows the ack). */
        stored1(b, job, &hash);
        if (why == MBD_WHY_PUT) ack(b, from, job, &hash, true);
        for (size_t i = 0; i < owed.n; i++) ack(b, owed.v[i], job, &hash, true);
    } else {
        /* Failed, or the job was forgotten while it was written (no copy kept). */
        char *w = word(job), *x = hexs(&hash);
        char *e = ok ? mfmt("job %s forgotten meanwhile", w) : mstrdup(err ? err : "write failed");
        say(b, true, mfmt("blobs: job %s's copy of %s: %s", w, x, e));
        free(w);
        free(x);
        free(e);
        if (why == MBD_WHY_PUT) ack(b, from, job, &hash, false);
        else if (why == MBD_WHY_LOCAL) b->io.acked(b->io.ctx, b->me, job, &hash, false);
        for (size_t i = 0; i < owed.n; i++) ack(b, owed.v[i], job, &hash, false);
    }
    free(owed.v);
}

void mbd_written(mbd *b, mj_job_id job, mj_hash hash, mbd_why why, uint32_t from, bool ok, const char *err) { written(b, job, hash, why, from, ok, err); }

/* A blob for an assembly's item: folded if it is still wanted; false if refused (its digest). */
static bool fold_in(mbd *b, asmx *x, uint64_t item, const uint8_t *bytes, size_t n) {
    char *e = NULL;
    if (masm_blob(x->a, item, bytes, n, &e) >= 0) return true;
    char *w = word(masm_job(x->a));
    say(b, true, mfmt("render job %s: %s", w, e));
    free(w);
    free(e);
    return false;
}

/* A fetched blob's bytes (from a member, or here meanwhile): the jobs that keep it get their reference,
 * the assemblies that wait for it fold it. Takes f's contents and bytes (may be NULL). */
static void resolve(mbd *b, const mj_hash *hash, fetch *f, uint8_t *bytes, size_t n) {
    if (!bytes) bytes = bytes_of(b, hash, &n);
    if (bytes) {
        for (size_t i = 0; i < f->keep.n; i++) {
            uint8_t *c = mxalloc(n ? n : 1);
            memcpy(c, bytes, n);
            char *e = NULL;
            if (!ensure(b, f->keep.v[i], *hash, c, n, MBD_WHY_FETCHED, &e)) {
                char *x = hexs(hash), *w = word(f->keep.v[i]);
                say(b, true, mfmt("blobs: fetched %s for job %s: %s", x, w, e));
                free(x);
                free(w);
                free(e);
            }
        }
        for (size_t i = 0; i < f->nitems; i++) {
            asmx *a = momap_get1(b->asm_, f->items[i].job);
            if (a) fold_in(b, a, f->items[i].item, bytes, n);
        }
        free(bytes);
    }
    fetch_free(f);
}

void mbd_frame(mbd *b, uint32_t from, uint8_t ty, const uint8_t *body, size_t n) {
    mbf_body x;
    if (n < 32) {
        say(b, true, mfmt("blobs: a frame of type %u from system %u: short", ty, from));
        return;
    }
    if (!mbf_decode(ty, body, n, &x)) return;
    switch (ty) {
    case MESH_F_BLOB_PUT: {
        mj_hash got;
        mesh_sha256(x.bytes, x.nbytes, got.b);
        if (x.nbytes != x.len || !mj_hash_eq(&got, &x.hash)) {
            b->bad_in++;
            char *h = hexs(&x.hash);
            say(b, true, mfmt("blobs: system %u pushed a blob that is not %s (dropped)", from, h));
            free(h);
            return;
        }
        b->puts_in++;
        /* This job's copy: here already (acked), being made (acked when durable), else made now. */
        if (mb_has_ref(b->store, x.job, &x.hash)) {
            ack(b, from, x.job, &x.hash, true);
            return;
        }
        if (mb_known_ref(b->store, x.job, &x.hash)) {
            uint64_t k[5];
            k5(k, x.job, &x.hash);
            u32list *o = momap_put(b->owed, k, NULL);
            o->v = mxrealloc(o->v, (o->n + 1) * sizeof *o->v);
            o->v[o->n++] = from;
            return;
        }
        uint8_t *c = mxalloc(x.nbytes ? x.nbytes : 1);
        memcpy(c, x.bytes, x.nbytes);
        char *e = NULL;
        if (!keep(b, x.job, x.hash, c, x.nbytes, MBD_WHY_PUT, from, &e)) {
            if (!b->full_logged_set || !strstr(e, "full")) {
                char *h = hexs(&x.hash), *w = word(x.job);
                say(b, true, mfmt("blobs: system %u's blob %s of job %s: %s", from, h, w, e));
                free(h);
                free(w);
            }
            free(e);
            ack(b, from, x.job, &x.hash, false);
        }
        break;
    }
    case MESH_F_BLOB_ACK: {
        uint64_t k[6];
        k6(k, from, x.job, &x.hash);
        momap_del(b->inflight, k);
        b->io.acked(b->io.ctx, from, x.job, &x.hash, x.ok);
        break;
    }
    case MESH_F_BLOB_GET: {
        size_t bn = 0;
        uint8_t *found = bytes_of(b, &x.hash, &bn);
        msb f = {0};
        mbf_blob(&f, &x.hash, found, bn, found != NULL);
        if (found) {
            if (b->io.send_bulk(b->io.ctx, from, (const uint8_t *)f.p, f.n)) b->gets_served++;
        } else b->io.send_peer(b->io.ctx, from, (const uint8_t *)f.p, f.n);
        free(found);
        msb_free(&f);
        break;
    }
    case MESH_F_BLOB: {
        uint64_t k[4];
        hk(k, &x.hash);
        fetch *f = momap_get(b->fetches, k);
        if (!f) return;
        if (f->asked && f->asked_k == from) f->asked = false;
        if (!x.ok) {
            f->absent |= (uint32_t)1 << from;
            return;
        }
        mj_hash got;
        mesh_sha256(x.bytes, x.nbytes, got.b);
        if (!mj_hash_eq(&got, &x.hash)) {
            b->bad_in++;
            char *h = hexs(&x.hash);
            say(b, true, mfmt("blobs: system %u sent a blob that is not %s (dropped; asking another holder)", from, h));
            free(h);
            return;
        }
        fetch fx;
        momap_take(b->fetches, k, &fx);
        uint8_t *c = mxalloc(x.nbytes ? x.nbytes : 1);
        memcpy(c, x.bytes, x.nbytes);
        resolve(b, &x.hash, &fx, c, x.nbytes);
        break;
    }
    default: break;
    }
}

static mbd_job_info info_clone(const mbd_job_info *j) {
    mbd_job_info c = *j;
    c.program = mstrdup(j->program);
    c.setup = mj_setup_clone(j->setup, j->nsetup);
    c.state = mstrdup(j->state);
    return c;
}

/* Start assembling job id (jobd sends its results from now on). */
static bool watch(mbd *b, mj_job_id id, char **err) {
    if (momap_get1(b->asm_, id)) return true;
    const mbd_job_info *j = momap_get1(b->jobs, id);
    char *w = word(id);
    if (!j) {
        *err = mfmt("no job %s (committed here)", w);
        free(w);
        return false;
    }
    if (!masm_renders(j->program, j->op)) {
        *err = mfmt("job %s (%s op %u) is not a pathtrace image", w, j->program, j->op);
        free(w);
        return false;
    }
    free(w);
    masm *a = masm_new(id, j->setup, j->nsetup, j->tone, err);
    if (!a) return false;
    asmx *x = momap_put1(b->asm_, id, NULL);
    *x = (asmx){a, b->io.now_ms(b->io.ctx), NULL};
    b->io.watching(b->io.ctx, id, true);
    return true;
}

void mbd_table(mbd *b, mbd_news *n) {
    for (size_t i = 0; i < n->njobs; i++) {
        asmx *a = momap_get1(b->asm_, n->jobs[i].id);
        if (a && masm_tone(a->a) != n->jobs[i].tone) {
            masm_set_tone(a->a, n->jobs[i].tone);
            free(a->sha);
            a->sha = NULL;
        }
    }
    momap_clear(b->jobs);
    for (size_t i = 0; i < n->njobs; i++) *(mbd_job_info *)momap_put1(b->jobs, n->jobs[i].id, NULL) = info_clone(&n->jobs[i]);
    for (size_t i = 0; i < n->nforgotten; i++) {
        mj_job_id id = n->forgotten[i];
        momap_put1(b->forgotten, id, NULL);
        momap_del1(b->asm_, id);
        b->io.watching(b->io.ctx, id, false);
        uint64_t k[4];
        for (fetch *f = momap_first(b->fetches, k); f; f = momap_next(b->fetches, k, k)) {
            js_del(&f->keep, id);
            size_t kept = 0;
            for (size_t q = 0; q < f->nitems; q++)
                if (f->items[q].job != id) f->items[kept++] = f->items[q];
            f->nitems = kept;
            if (!f->keep.n && !f->nitems) momap_del(b->fetches, k);
        }
        mb_refs gone = mb_drop_job(b->store, id);
        if (gone.n) {
            char *w = word(id);
            say(b, false, mfmt("blobs: job %s forgotten: its %zu reference(s) removed (%zu blob(s), %llu MiB, left for other jobs)", w, gone.n,
                               mb_count(b->store), (unsigned long long)(mb_bytes(b->store) >> 20)));
            free(w);
            b->io.gone(b->io.ctx, gone.v, gone.n);
        }
        mb_refs_free(&gone);
    }
    for (size_t i = 0; i < n->nresults; i++) {
        asmx *a = momap_get1(b->asm_, n->results[i].job);
        if (a) masm_commit(a->a, n->results[i].item, n->results[i].it);
    }
    /* The leader assembles every active image (its blobs are all here). */
    if (b->io.leading(b->io.ctx)) {
        uint64_t id;
        momap_iter it;
        momap_iter_init(&it, b->jobs);
        mj_job_id act[1024];
        size_t na = 0;
        for (const mbd_job_info *j = momap_iter_next1(&it, &id); j; j = momap_iter_next1(&it, &id))
            if (j->active && masm_renders(j->program, j->op) && na < 1024) act[na++] = id;
        for (size_t i = 0; i < na; i++) {
            char *e = NULL;
            if (!watch(b, act[i], &e)) free(e);
        }
    }
    mbd_news_free(n);
}

static bool asked_by(void *ctx, uint64_t item);

typedef struct {
    const mbd *b;
    mj_job_id id;
    const masm *a;
} asked_cx;

static bool asked_by(void *ctx, uint64_t item) {
    const asked_cx *c = ctx;
    const masm_item *it = masm_wanted(c->a, item);
    if (!it) return false;
    uint64_t k[4];
    hk(k, &it->hash);
    const fetch *f = momap_get(c->b->fetches, k);
    if (!f) return false;
    for (size_t i = 0; i < f->nitems; i++)
        if (f->items[i].job == c->id && f->items[i].item == item) return true;
    return false;
}

static int lowest(uint32_t mask, const uint32_t asked[32]) {
    int best = -1;
    for (int k = 0; k < 32; k++)
        if ((mask >> k & 1) && (best < 0 || asked[k] < asked[best])) best = k;
    return best;
}

void mbd_tick(mbd *b) {
    int64_t now = b->io.now_ms(b->io.ctx);
    uint32_t alive = b->io.alive(b->io.ctx) | (uint32_t)1 << b->me;
    /* Pushes: per member at most PUT_WINDOW unacknowledged; a lost member's queue dropped. */
    uint64_t k6k[6];
    for (int64_t *t = momap_first(b->inflight, k6k); t; t = momap_next(b->inflight, k6k, k6k))
        if (now - *t >= (int64_t)MESH_BLOBD_PUT_TIMEOUT_MS) momap_del(b->inflight, k6k);
    size_t per[32] = {0};
    for (int64_t *t = momap_first(b->inflight, k6k); t; t = momap_next(b->inflight, k6k, k6k))
        if (k6k[0] < 32) per[k6k[0]]++;
    uint32_t blocked = 0;
    size_t keep = 0;
    for (size_t i = 0; i < b->npushq; i++) {
        pushk p = b->pushq[i];
        uint64_t k[6];
        k6(k, p.to, p.job, &p.h);
        if (!(alive >> p.to & 1)) {
            momap_del(b->queued, k);
            continue;
        }
        if ((blocked >> p.to & 1) || per[p.to] >= MESH_BLOBD_PUT_WINDOW) {
            blocked |= (uint32_t)1 << p.to;
            b->pushq[keep++] = p;
            continue;
        }
        size_t n;
        uint8_t *bytes = bytes_of(b, &p.h, &n);
        if (!bytes) {
            momap_del(b->queued, k);
            char *x = hexs(&p.h);
            say(b, true, mfmt("blobs: %s to push is gone", x));
            free(x);
            continue;
        }
        msb f = {0};
        mbf_put(&f, &p.h, p.job, bytes, (uint32_t)n);
        free(bytes);
        if (b->io.send_bulk(b->io.ctx, p.to, (const uint8_t *)f.p, f.n)) {
            momap_del(b->queued, k);
            *(int64_t *)momap_put(b->inflight, k, NULL) = now;
            per[p.to]++;
            b->puts_out++;
        } else {
            blocked |= (uint32_t)1 << p.to;
            b->pushq[keep++] = p;
        }
        msb_free(&f);
    }
    b->npushq = keep;
    /* The assemblies' next passes: read here, or fetched. */
    size_t reads = 0;
    uint64_t id = 0;
    for (asmx *x = momap_first(b->asm_, &id); x; x = momap_next(b->asm_, &id, &id)) {
        size_t fetching = momap_len(b->fetches);
        size_t avail = MESH_BLOBD_FETCH_MAX > fetching ? MESH_BLOBD_FETCH_MAX - fetching : 0;
        size_t budget = avail + MESH_BLOBD_LOCAL_READS > reads ? avail + MESH_BLOBD_LOCAL_READS - reads : 0;
        if (!budget) break;
        uint64_t *items = mxalloc(budget * sizeof *items);
        masm_item *its = mxalloc(budget * sizeof *its);
        asked_cx cx = {b, id, x->a};
        size_t nw = masm_wants(x->a, asked_by, &cx, budget, items, its);
        for (size_t i = 0; i < nw; i++) {
            if (here(b, &its[i].hash)) {
                /* Here: read it (the next tick if this one read enough). */
                if (reads >= MESH_BLOBD_LOCAL_READS) continue;
                reads++;
                size_t n;
                uint8_t *bytes = bytes_of(b, &its[i].hash, &n);
                if (bytes) {
                    bool ok = fold_in(b, x, items[i], bytes, n);
                    free(bytes);
                    if (ok) continue;
                }
            }
            if (momap_len(b->fetches) >= MESH_BLOBD_FETCH_MAX) continue;
            fetch *f = fetch_of(b, &its[i].hash, id, its[i].holders);
            f->holders |= its[i].holders;
            bool has = false;
            for (size_t q = 0; q < f->nitems; q++) has |= f->items[q].job == id && f->items[q].item == items[i];
            if (!has) {
                f->items = mxrealloc(f->items, (f->nitems + 1) * sizeof *f->items);
                f->items[f->nitems++] = (jobitem){id, items[i]};
            }
        }
        free(items);
        free(its);
    }
    /* A blob fetched that is here meanwhile (written, pushed here, for any job) is read instead. */
    uint64_t hk4[4];
    for (fetch *f = momap_first(b->fetches, hk4); f; f = momap_next(b->fetches, hk4, hk4)) {
        mj_hash h = mj_hash_of_words(hk4);
        if (!here(b, &h)) continue;
        fetch fx;
        momap_take(b->fetches, hk4, &fx);
        resolve(b, &h, &fx, NULL, 0);
    }
    /* Fetches: one holder at a time, the next on a refusal or a timeout. */
    uint32_t asked[32] = {0};
    for (fetch *f = momap_first(b->fetches, hk4); f; f = momap_next(b->fetches, hk4, hk4))
        if (f->asked && f->asked_k < 32) asked[f->asked_k]++;
    for (fetch *f = momap_first(b->fetches, hk4); f; f = momap_next(b->fetches, hk4, hk4)) {
        mj_hash h = mj_hash_of_words(hk4);
        if (f->asked) {
            if (now - f->asked_at < (int64_t)MESH_BLOBD_FETCH_TIMEOUT_MS) continue;
            f->asked = false;
        }
        if (now < f->after) continue;
        uint32_t not_me = ~((uint32_t)1 << b->me);
        int k = lowest(f->holders & alive & ~f->tried & not_me, asked);
        if (k < 0) k = lowest(alive & ~f->tried & not_me, asked);
        if (k < 0) {
            /* Every member asked: a fetch for the leader's commit ends; an assembly's starts over in a while.
             * Every member up said "not here": held by no member up -- said loudly. */
            if (!f->nitems) {
                momap_del(b->fetches, hk4);
                continue;
            }
            uint32_t others = alive & not_me;
            if (others && (others & ~f->absent) == 0 && !f->lost) {
                f->lost = true;
                char *x = hexs(&h), *w = word(f->items[0].job);
                say(b, true, mfmt("render: blob %s (job %s item %llu, holders 0x%x) is held by no member up", x, w, (unsigned long long)f->items[0].item,
                                  f->holders));
                free(x);
                free(w);
            }
            f->tried = 0;
            f->absent = 0;
            f->after = now + MESH_BLOBD_FETCH_TIMEOUT_MS;
            continue;
        }
        f->tried |= (uint32_t)1 << k;
        f->asked = true;
        f->asked_k = (uint32_t)k;
        f->asked_at = now;
        asked[k]++;
        msb fr = {0};
        mbf_get(&fr, &h, f->job);
        b->io.send_peer(b->io.ctx, (uint32_t)k, (const uint8_t *)fr.p, fr.n);
        msb_free(&fr);
    }
    /* An assembly nobody asked for in a while stops (not the leader's active ones: news restarts them). */
    for (asmx *x = momap_first(b->asm_, &id); x; x = momap_next(b->asm_, &id, &id)) {
        const mbd_job_info *j = momap_get1(b->jobs, id);
        if (now - x->asked_at > 600000 && !(j && j->active)) {
            momap_del1(b->asm_, id);
            b->io.watching(b->io.ctx, id, false);
        }
    }
}

void mbd_report_lost(mbd *b) {
    mb_refs lost = mb_take_lost(b->store);
    if (lost.n) b->io.gone(b->io.ctx, lost.v, lost.n);
    mb_refs_free(&lost);
}

bool mbd_render(mbd *b, const char *const *w, size_t nw, uint8_t **out, size_t *len, char **err) {
    const char *job = "latest", *what = "status";
    if (nw == 1) job = w[0];
    else if (nw == 2) job = w[0], what = w[1];
    else if (nw > 2) {
        *err = mstrdup("usage: render [<job> | latest] [status | ppm | bmp | raw]");
        return false;
    }
    mj_job_id id = 0;
    if (strcmp(job, "latest") == 0) {
        bool any = false;
        uint64_t k;
        for (const mbd_job_info *j = momap_first(b->jobs, &k); j; j = momap_next(b->jobs, &k, &k))
            if (masm_renders(j->program, j->op)) {
                id = k;
                any = true;
            }
        if (!any) {
            *err = mstrdup("no pathtrace job in the committed table");
            return false;
        }
    } else if (!mj_parse_job(job, &id)) {
        *err = mfmt("\"%s\": a job id is <term>.<n>, or latest", job);
        return false;
    }
    if (!watch(b, id, err)) return false;
    const mbd_job_info *info = momap_get1(b->jobs, id);
    size_t fetching = 0, unavailable = 0;
    uint64_t hk4[4];
    for (const fetch *f = momap_first(b->fetches, hk4); f; f = momap_next(b->fetches, hk4, hk4)) {
        bool mine = false;
        for (size_t i = 0; i < f->nitems; i++) mine |= f->items[i].job == id;
        fetching += mine;
        unavailable += mine && f->lost;
    }
    asmx *x = momap_get1(b->asm_, id);
    x->asked_at = b->io.now_ms(b->io.ctx);
    masm *a = x->a;
    if (strcmp(what, "ppm") == 0) {
        *out = masm_ppm(a, len);
        return true;
    }
    if (strcmp(what, "bmp") == 0) {
        *out = masm_bmp_of(a, len);
        return true;
    }
    if (strcmp(what, "status") != 0 && strcmp(what, "raw") != 0) {
        *err = mfmt("render: \"%s\": status, ppm, bmp or raw", what);
        return false;
    }
    uint64_t dacc, dpix;
    masm_digests(a, &dacc, &dpix);
    if (masm_complete(a) && !x->sha) x->sha = masm_sha256(a);
    char *w2 = word(id);
    msb s = {0};
    msb_printf(&s, "render job %s program %s state %s system %u width %u height %u tiles %u passes %u spp %u items %llu committed %llu folded %llu tiles_done %u tone %s fetching %zu unavailable %zu bad %zu complete %u\n",
               w2, info ? info->program : "?", info ? info->state : "?", b->me, masm_width(a), masm_height(a), masm_tiles(a), masm_passes(a), masm_spp(a),
               (unsigned long long)masm_items(a), (unsigned long long)masm_committed(a), (unsigned long long)masm_folded(a), masm_tiles_done(a),
               masm_tone(a) == 1 ? "reinhard" : "aces", fetching, unavailable, masm_bad_count(a), masm_complete(a) ? 1u : 0u);
    free(w2);
    msb_printf(&s, "digests acc %016llx pixels %016llx sha256 %s\n", (unsigned long long)dacc, (unsigned long long)dpix, x->sha ? x->sha : "-");
    uint32_t tiles_by[32] = {0};
    for (uint32_t t = 0; t < masm_tiles(a); t++) {
        uint32_t np, o;
        masm_tile(a, t, &np, &o);
        if (np > 0 && o < 32) tiles_by[o]++;
    }
    uint64_t m;
    for (const uint64_t *c = momap_first(masm_by_member(a), &m); c; c = momap_next(masm_by_member(a), &m, &m))
        msb_printf(&s, "member %llu items %llu tiles %u\n", (unsigned long long)m, (unsigned long long)*c, m < 32 ? tiles_by[m] : 0);
    msb_printf(&s, "store blobs %zu bytes %llu puts_in %llu puts_out %llu gets_served %llu bad_in %llu refs %llu cap %llu refused %llu\n", mb_count(b->store),
               (unsigned long long)mb_bytes(b->store), (unsigned long long)b->puts_in, (unsigned long long)b->puts_out,
               (unsigned long long)b->gets_served, (unsigned long long)b->bad_in, (unsigned long long)mb_refs_total(b->store),
               (unsigned long long)mb_cap(b->store), (unsigned long long)b->refused);
    for (size_t i = 0; i < masm_bad_count(a) && i < 5; i++) msb_printf(&s, "bad %s\n", masm_bad_at(a, i, NULL));
    if (strcmp(what, "status") == 0) {
        *len = s.n;
        *out = (uint8_t *)msb_take(&s);
        return true;
    }
    /* raw (mesh-top's Render tab): the text, a 0 byte, u32 tiles, per tile (passes folded, member of the
     * last), and the 8-bit pixels (tile-major 0xffBBGGRR). */
    msb_putc(&s, 0);
    uint32_t tiles = masm_tiles(a);
    msb_put(&s, &tiles, 4);
    for (uint32_t t = 0; t < tiles; t++) {
        uint32_t np, o;
        masm_tile(a, t, &np, &o);
        msb_put(&s, &np, 4);
        msb_put(&s, &o, 4);
    }
    msb_put(&s, masm_rgb(a), (size_t)tiles * PT_TILE_PIX * 4);
    *len = s.n;
    *out = (uint8_t *)msb_take(&s);
    return true;
}

void mbd_write_batch(mbd_write *ws, size_t n, bool *ok, char **err) {
    for (size_t i = 0; i < n; i++) {
        char x[65];
        mj_hex(&ws[i].hash, x);
        err[i] = NULL;
        ok[i] = mb_make_durable(ws[i].dir, x, ws[i].link, ws[i].bytes, ws[i].n, &err[i]);
    }
    /* Each directory fsynced once; a failure fails every write that had succeeded. */
    for (size_t i = 0; i < n; i++) {
        bool seen = false;
        for (size_t j = 0; j < i; j++) seen |= strcmp(ws[j].dir, ws[i].dir) == 0;
        if (seen) continue;
        char *e = NULL;
        if (!mb_sync_dir(ws[i].dir, &e)) {
            for (size_t j = 0; j < n; j++)
                if (ok[j]) {
                    ok[j] = false;
                    err[j] = mfmt("fsync %s: %s", ws[i].dir, e);
                }
        }
        free(e);
    }
}
