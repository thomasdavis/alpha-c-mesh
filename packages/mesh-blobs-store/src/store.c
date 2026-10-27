/* blobs.rs's Store. */
#include "mesh/blobs/store.h"
#include "mesh/blobs/durable.h"
#include "mesh/omap.h"
#include "mesh/ref/sha256d.h"
#include "mesh/strbuf.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { PENDING = 1, PRESENT = 2 };

typedef struct {
    uint32_t len;
    momap *refs; /* job -> uint8_t state */
} entry;

typedef struct {
    uint8_t *p;
    size_t n;
} bytes;

struct mb_store {
    char *dir;
    uint64_t cap;
    momap *index; /* hash words -> entry */
    momap *jobs;  /* job -> uint64_t references (pending included) */
    uint64_t total;
    momap *mem;   /* hash words -> bytes (no directory) */
    mb_refs lost;
    void (*log)(void *ctx, const char *line);
    void *log_ctx;
};

static void entry_free(void *v) { momap_free(((entry *)v)->refs); }
static void bytes_free(void *v) { free(((bytes *)v)->p); }

void mb_refs_free(mb_refs *r) {
    free(r->v);
    *r = (mb_refs){0};
}

static void refs_push(mb_refs *r, mj_job_id job, const mj_hash *h) {
    r->v = mxrealloc(r->v, (r->n + 1) * sizeof *r->v);
    r->v[r->n++] = (mb_ref){job, *h};
}

void mb_reserved_free(mb_reserved *r) {
    free(r->path);
    mb_refs_free(&r->evicted);
    *r = (mb_reserved){0};
}

static void add_ref(mb_store *s, mj_job_id job, const mj_hash *h, uint32_t len, uint8_t st) {
    uint64_t k[4];
    mj_hash_words(h, k);
    bool fresh;
    entry *e = momap_put(s->index, k, &fresh);
    if (fresh) {
        s->total += len;
        e->len = len;
        e->refs = momap_new(1, 1, NULL, NULL);
    }
    bool newref;
    uint8_t *r = momap_put1(e->refs, job, &newref);
    *r = st;
    if (newref) (*(uint64_t *)momap_put1(s->jobs, job, NULL))++;
}

mb_store *mb_open(const char *dir, uint64_t cap, char **err) {
    mb_store *s = mxalloc(sizeof *s);
    s->cap = cap;
    s->index = momap_new(4, sizeof(entry), entry_free, NULL);
    s->jobs = momap_new(1, sizeof(uint64_t), NULL, NULL);
    s->mem = momap_new(4, sizeof(bytes), bytes_free, NULL);
    if (!dir) return s;
    s->dir = mstrdup(dir);
    char *e = NULL;
    if (!mb_mkdirs(dir, &e)) {
        if (err) *err = mfmt("--blobs %s: %s", dir, e);
        free(e);
        mb_store_free(s);
        return NULL;
    }
    DIR *d = opendir(dir);
    if (!d) {
        if (err) *err = mfmt("--blobs %s: cannot read it", dir);
        mb_store_free(s);
        return NULL;
    }
    struct dirent *j;
    while ((j = readdir(d))) {
        mj_job_id job;
        if (!mj_parse_job(j->d_name, &job)) continue;
        char *jd = mfmt("%s/%s", dir, j->d_name);
        DIR *fd = opendir(jd);
        if (fd) {
            struct dirent *f;
            while ((f = readdir(fd))) {
                size_t nl = strlen(f->d_name);
                char *p = mfmt("%s/%s", jd, f->d_name);
                if (nl > 4 && strcmp(f->d_name + nl - 4, ".tmp") == 0) unlink(p);
                else {
                    mj_hash h;
                    struct stat st;
                    if (mj_unhex(f->d_name, &h) && stat(p, &st) == 0) add_ref(s, job, &h, (uint32_t)st.st_size, PRESENT);
                }
                free(p);
            }
            closedir(fd);
        }
        free(jd);
    }
    closedir(d);
    return s;
}

void mb_store_free(mb_store *s) {
    if (!s) return;
    free(s->dir);
    momap_free(s->index);
    momap_free(s->jobs);
    momap_free(s->mem);
    mb_refs_free(&s->lost);
    free(s);
}

void mb_set_log(mb_store *s, void (*log)(void *ctx, const char *line), void *ctx) {
    s->log = log;
    s->log_ctx = ctx;
}

uint64_t mb_cap(const mb_store *s) { return s->cap; }
bool mb_persisted(const mb_store *s) { return s->dir != NULL; }
uint64_t mb_bytes(const mb_store *s) { return s->total; }
size_t mb_count(const mb_store *s) { return momap_len(s->index); }

uint64_t mb_refs_total(const mb_store *s) {
    uint64_t t = 0, k;
    for (const uint64_t *c = momap_first(s->jobs, &k); c; c = momap_next(s->jobs, &k, &k)) t += *c;
    return t;
}

static entry *entry_of(const mb_store *s, const mj_hash *h) {
    uint64_t k[4];
    mj_hash_words(h, k);
    return momap_get(s->index, k);
}

/* The first reference (in job order) that is present. */
static bool present(const entry *e, mj_job_id *job) {
    uint64_t j;
    for (const uint8_t *st = momap_first(e->refs, &j); st; st = momap_next(e->refs, &j, &j))
        if (*st == PRESENT) {
            *job = j;
            return true;
        }
    return false;
}

bool mb_has(const mb_store *s, const mj_hash *h) {
    const entry *e = entry_of(s, h);
    mj_job_id j;
    return e && present(e, &j);
}

bool mb_has_ref(const mb_store *s, mj_job_id job, const mj_hash *h) {
    const entry *e = entry_of(s, h);
    const uint8_t *st = e ? momap_get1(e->refs, job) : NULL;
    return st && *st == PRESENT;
}

bool mb_known_ref(const mb_store *s, mj_job_id job, const mj_hash *h) {
    const entry *e = entry_of(s, h);
    return e && momap_get1(e->refs, job) != NULL;
}

mb_refs mb_all(const mb_store *s) {
    mb_refs r = {0};
    uint64_t k[4];
    for (const entry *e = momap_first(s->index, k); e; e = momap_next(s->index, k, k)) {
        mj_hash h = mj_hash_of_words(k);
        uint64_t j;
        for (const uint8_t *st = momap_first(e->refs, &j); st; st = momap_next(e->refs, &j, &j))
            if (*st == PRESENT) refs_push(&r, j, &h);
    }
    return r;
}

char *mb_dir_of(const mb_store *s, mj_job_id job) {
    if (!s->dir) return NULL;
    char w[MJ_JOB_WORD_MAX];
    mj_job_word(job, w);
    return mfmt("%s/%s", s->dir, w);
}

static char *path_of(const mb_store *s, mj_job_id job, const mj_hash *h) {
    char *d = mb_dir_of(s, job);
    if (!d) return NULL;
    char x[65];
    mj_hex(h, x);
    char *p = mfmt("%s/%s", d, x);
    free(d);
    return p;
}

/* Job's reference to h removed (the file under its name too); the blob's bytes go with its last reference. */
static void unref(mb_store *s, mj_job_id job, const mj_hash *h) {
    uint64_t k[4];
    mj_hash_words(h, k);
    entry *e = momap_get(s->index, k);
    if (!e || !momap_del1(e->refs, job)) return;
    char *p = path_of(s, job, h);
    if (p) unlink(p);
    free(p);
    uint64_t *c = momap_get1(s->jobs, job);
    if (c && --*c == 0) momap_del1(s->jobs, job);
    if (momap_len(e->refs) == 0) {
        s->total -= e->len;
        momap_del(s->index, k);
        momap_del(s->mem, k);
    }
}

mb_refs mb_drop_job(mb_store *s, mj_job_id job) {
    mb_refs hs = {0};
    uint64_t k[4];
    for (const entry *e = momap_first(s->index, k); e; e = momap_next(s->index, k, k))
        if (momap_get1(e->refs, job)) {
            mj_hash h = mj_hash_of_words(k);
            refs_push(&hs, job, &h);
        }
    for (size_t i = 0; i < hs.n; i++) unref(s, job, &hs.v[i].h);
    momap_del1(s->jobs, job);
    char *d = mb_dir_of(s, job);
    if (d) mb_remove_tree(d);
    free(d);
    return hs;
}

bool mb_reserve(mb_store *s, mj_job_id job, mj_hash h, uint32_t len, mb_live_fn live, void *live_ctx, mb_reserved *out, char **err) {
    if (mb_known_ref(s, job, &h)) {
        char w[MJ_JOB_WORD_MAX], x[65];
        mj_job_word(job, w);
        mj_hex(&h, x);
        fprintf(stderr, "reserve: job %s's reference to %s exists\n", w, x);
        abort();
    }
    *out = (mb_reserved){0};
    entry *e = entry_of(s, &h);
    if (e) {
        mj_job_id j;
        if (present(e, &j)) {
            out->link = true;
            out->path = path_of(s, j, &h);
            if (!out->path) out->path = mstrdup("");
        }
        add_ref(s, job, &h, len, PENDING);
        return true;
    }
    while (s->total + len > s->cap) {
        bool found = false;
        uint64_t v;
        for (const uint64_t *c = momap_first(s->jobs, &v); c; c = momap_next(s->jobs, &v, &v))
            if (v != job && live(live_ctx, v) == MB_FORGOTTEN) {
                found = true;
                break;
            }
        if (!found) {
            if (err)
                *err = mfmt("blob store full: %llu of %llu MiB, every blob held for a job in the table (job forget a finished one to make room)",
                            (unsigned long long)(s->total >> 20), (unsigned long long)(s->cap >> 20));
            mb_refs_free(&out->evicted);
            return false;
        }
        mb_refs gone = mb_drop_job(s, v);
        for (size_t i = 0; i < gone.n; i++) refs_push(&out->evicted, v, &gone.v[i].h);
        mb_refs_free(&gone);
    }
    add_ref(s, job, &h, len, PENDING);
    return true;
}

void mb_written(mb_store *s, mj_job_id job, const mj_hash *h, bool ok) {
    if (!ok) {
        unref(s, job, h);
        return;
    }
    entry *e = entry_of(s, h);
    uint8_t *st = e ? momap_get1(e->refs, job) : NULL;
    if (st) *st = PRESENT;
    else {
        char *p = path_of(s, job, h);
        if (p) unlink(p);
        free(p);
    }
}

void mb_put_mem(mb_store *s, mj_job_id job, mj_hash h, const void *b, size_t n) {
    uint64_t k[4];
    mj_hash_words(&h, k);
    bool fresh;
    bytes *x = momap_put(s->mem, k, &fresh);
    if (fresh) {
        x->p = mxalloc(n);
        memcpy(x->p, b, n);
        x->n = n;
    }
    mb_written(s, job, &h, true);
}

static uint8_t *read_file(const char *p, size_t *n) {
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    msb b = {0};
    char buf[65536];
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0) msb_put(&b, buf, r);
    bool bad = ferror(f);
    fclose(f);
    if (bad) {
        msb_free(&b);
        return NULL;
    }
    *n = b.n;
    if (!b.p) return mxalloc(1);
    return (uint8_t *)msb_take(&b);
}

uint8_t *mb_get(mb_store *s, const mj_hash *h, size_t *n) {
    entry *e = entry_of(s, h);
    if (!e) return NULL;
    size_t np = 0;
    mj_job_id *pres = NULL;
    uint64_t j;
    for (const uint8_t *st = momap_first(e->refs, &j); st; st = momap_next(e->refs, &j, &j))
        if (*st == PRESENT) {
            pres = mxrealloc(pres, (np + 1) * sizeof *pres);
            pres[np++] = j;
        }
    for (size_t i = 0; i < np; i++) {
        uint8_t *b = NULL;
        size_t bn = 0;
        if (!s->dir) {
            uint64_t k[4];
            mj_hash_words(h, k);
            const bytes *x = momap_get(s->mem, k);
            if (x) {
                b = mxalloc(x->n ? x->n : 1);
                memcpy(b, x->p, x->n);
                bn = x->n;
            }
        } else {
            char *p = path_of(s, pres[i], h);
            b = read_file(p, &bn);
            free(p);
        }
        if (b) {
            mj_hash got;
            mesh_sha256(b, bn, got.b);
            if (mj_hash_eq(&got, h)) {
                free(pres);
                *n = bn;
                return b;
            }
            free(b);
        }
        if (s->log) {
            char x[65], w[MJ_JOB_WORD_MAX];
            mj_hex(h, x);
            mj_job_word(pres[i], w);
            char *line = mfmt("blobs: %s of job %s is missing or corrupt: dropped", x, w);
            s->log(s->log_ctx, line);
            free(line);
        }
        unref(s, pres[i], h);
        refs_push(&s->lost, pres[i], h);
    }
    free(pres);
    return NULL;
}

mb_refs mb_take_lost(mb_store *s) {
    mb_refs r = s->lost;
    s->lost = (mb_refs){0};
    return r;
}

size_t mb_jobs(const mb_store *s, mj_job_id **out) {
    size_t n = momap_len(s->jobs), i = 0;
    *out = n ? mxalloc(n * sizeof **out) : NULL;
    uint64_t j;
    for (const void *c = momap_first(s->jobs, &j); c; c = momap_next(s->jobs, &j, &j)) (*out)[i++] = j;
    return n;
}
