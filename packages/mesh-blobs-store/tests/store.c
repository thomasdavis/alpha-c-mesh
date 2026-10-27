/* blobs.rs's tests: write-rename durability, hash verification, a blob of two jobs outlives either
 * (persisted and in memory), eviction (persisted and in memory). */
#include "mesh/blobs/store.h"
#include "mesh/blobs/durable.h"
#include "mesh/check.h"
#include "mesh/ref/sha256d.h"
#include "mesh/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *dir(const char *name) {
    char *d = mfmt("%s/meshd-blobs-%s-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", name, (int)getpid());
    mb_remove_tree(d);
    return d;
}

static mb_live listed(void *c, mj_job_id j) {
    (void)c;
    (void)j;
    return MB_LISTED;
}

static mj_hash sha(const void *b, size_t n) {
    mj_hash h;
    mesh_sha256(b, n, h.b);
    return h;
}

/* blobd's path: reserve, make durable (write or link), directory fsync, written. */
static bool put(mb_store *s, mj_job_id job, const void *b, size_t n, mb_live_fn live, mj_hash *h, mb_reserved *r, char **err) {
    *h = sha(b, n);
    if (!mb_reserve(s, job, *h, (uint32_t)n, live, NULL, r, err)) return false;
    char *d = mb_dir_of(s, job);
    if (d) {
        char x[65];
        mj_hex(h, x);
        CHECK(mb_make_durable(d, x, r->link ? r->path : NULL, b, n, NULL));
        CHECK(mb_sync_dir(d, NULL));
        mb_written(s, job, h, true);
        free(d);
    } else mb_put_mem(s, job, *h, b, n);
    return true;
}

static bool exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static void write_rename_durability(void) {
    char *d = dir("durable");
    mb_store *s = mb_open(d, 1 << 20, NULL);
    mj_job_id j = (uint64_t)1 << 32 | 1;
    mj_hash h;
    mb_reserved r;
    CHECK(put(s, j, "pass sums", 9, listed, &h, &r, NULL));
    CHECK(!r.link);
    mb_reserved_free(&r);
    size_t n;
    uint8_t *b = mb_get(s, &h, &n);
    CHECK(mb_has(s, &h) && mb_has_ref(s, j, &h) && b && n == 9 && memcmp(b, "pass sums", 9) == 0);
    free(b);
    char *jd = mfmt("%s/1.1", d), x[65];
    mj_hex(&h, x);
    char *f = mfmt("%s/%s", jd, x);
    CHECK(exists(f));
    /* A crash between the write and the rename: only the .tmp is there. */
    mj_hash torn = sha("half", 4);
    char tx[65];
    mj_hex(&torn, tx);
    char *tmp = mfmt("%s/%s.tmp", jd, tx);
    FILE *t = fopen(tmp, "wb");
    fputs("ha", t);
    fclose(t);
    /* A reserved reference is not present until written. */
    mj_hash p = sha("pending", 7);
    CHECK(mb_reserve(s, j, p, 7, listed, NULL, &r, NULL));
    mb_reserved_free(&r);
    CHECK(mb_known_ref(s, j, &p) && !mb_has(s, &p) && !mb_has_ref(s, j, &p));
    mb_written(s, j, &p, false);
    CHECK(!mb_known_ref(s, j, &p) && mb_bytes(s) == 9);
    mb_store_free(s);
    s = mb_open(d, 1 << 20, NULL);
    CHECK(mb_has_ref(s, j, &h) && !mb_has(s, &torn) && mb_count(s) == 1 && mb_bytes(s) == 9);
    CHECK(!exists(tmp));
    b = mb_get(s, &h, &n);
    CHECK(b && n == 9);
    free(b);
    mb_store_free(s);
    mb_remove_tree(d);
    free(d);
    free(jd);
    free(f);
    free(tmp);
}

static void hash_verification(void) {
    char *d = dir("verify");
    mb_store *s = mb_open(d, 1 << 20, NULL);
    mj_hash h;
    mb_reserved r;
    CHECK(put(s, 5, "payload", 7, listed, &h, &r, NULL));
    mb_reserved_free(&r);
    char x[65];
    mj_hex(&h, x);
    char *f = mfmt("%s/0.5/%s", d, x);
    FILE *t = fopen(f, "wb");
    fputs("pAyload", t);
    fclose(t);
    size_t n;
    CHECK(mb_get(s, &h, &n) == NULL);
    CHECK(!mb_known_ref(s, 5, &h) && mb_bytes(s) == 0);
    mb_refs lost = mb_take_lost(s);
    CHECK(lost.n == 1 && lost.v[0].job == 5 && mj_hash_eq(&lost.v[0].h, &h));
    mb_refs_free(&lost);
    mb_store_free(s);
    mb_remove_tree(d);
    free(d);
    free(f);
}

static void two_jobs(void) {
    for (int persisted = 1; persisted >= 0; persisted--) {
        char *d = dir("shared");
        mj_job_id a = (uint64_t)1 << 32 | 2, b = (uint64_t)2 << 32 | 1;
        mb_store *s = mb_open(persisted ? d : NULL, 1 << 20, NULL);
        mj_hash h, h2;
        mb_reserved r;
        CHECK(put(s, a, "identical pass sums", 19, listed, &h, &r, NULL));
        mb_reserved_free(&r);
        CHECK(mb_has(s, &h) && !mb_known_ref(s, b, &h)); /* job b has no copy until it makes one */
        CHECK(put(s, b, "identical pass sums", 19, listed, &h2, &r, NULL));
        CHECK(mj_hash_eq(&h, &h2));
        char x[65];
        mj_hex(&h, x);
        if (persisted) {
            char *want = mfmt("%s/1.2/%s", d, x), *other = mfmt("%s/2.1/%s", d, x);
            CHECK(r.link && strcmp(r.path, want) == 0);
            struct stat s1, s2;
            CHECK(stat(want, &s1) == 0 && stat(other, &s2) == 0 && s1.st_ino == s2.st_ino); /* a hard link, not a second copy */
            free(want);
            free(other);
        }
        mb_reserved_free(&r);
        CHECK(mb_has_ref(s, a, &h) && mb_has_ref(s, b, &h) && mb_count(s) == 1 && mb_refs_total(s) == 2 && mb_bytes(s) == 19);
        if (persisted) {
            mb_store_free(s);
            s = mb_open(d, 1 << 20, NULL);
            CHECK(mb_count(s) == 1 && mb_refs_total(s) == 2 && mb_bytes(s) == 19);
        }
        /* job forget a: b's copy stays. */
        mb_refs g = mb_drop_job(s, a);
        CHECK(g.n == 1 && mj_hash_eq(&g.v[0].h, &h));
        mb_refs_free(&g);
        size_t n;
        uint8_t *bytes = mb_get(s, &h, &n);
        CHECK(mb_has_ref(s, b, &h) && bytes && n == 19 && memcmp(bytes, "identical pass sums", 19) == 0 && mb_bytes(s) == 19);
        free(bytes);
        if (persisted) {
            char *ad = mfmt("%s/1.2", d);
            CHECK(!exists(ad));
            free(ad);
            mb_store_free(s);
            s = mb_open(d, 1 << 20, NULL);
            CHECK(mb_has_ref(s, b, &h));
        }
        g = mb_drop_job(s, b);
        mb_refs_free(&g);
        CHECK(!mb_has(s, &h) && mb_bytes(s) == 0 && mb_count(s) == 0);
        mb_store_free(s);
        mb_remove_tree(d);
        free(d);
    }
    /* A link whose source went meanwhile (its job forgotten while the writer ran): the bytes are written. */
    char *d = dir("shared-race");
    mb_store *s = mb_open(d, 1 << 20, NULL);
    mj_hash h;
    mb_reserved r;
    CHECK(put(s, 1, "bytes", 5, listed, &h, &r, NULL));
    mb_reserved_free(&r);
    CHECK(mb_reserve(s, 2, h, 5, listed, NULL, &r, NULL) && r.link);
    mb_refs g = mb_drop_job(s, 1);
    mb_refs_free(&g);
    char *d2 = mb_dir_of(s, 2), x[65];
    mj_hex(&h, x);
    CHECK(mb_make_durable(d2, x, r.path, "bytes", 5, NULL));
    mb_reserved_free(&r);
    mb_written(s, 2, &h, true);
    size_t n;
    uint8_t *b = mb_get(s, &h, &n);
    CHECK(b && n == 5 && memcmp(b, "bytes", 5) == 0);
    free(b);
    /* A reference dropped while its write ran leaves no file behind. */
    mj_hash late = sha("late", 4);
    CHECK(mb_reserve(s, 3, late, 4, listed, NULL, &r, NULL));
    mb_reserved_free(&r);
    g = mb_drop_job(s, 3);
    mb_refs_free(&g);
    char *d3 = mfmt("%s/0.3", d), lx[65];
    mj_hex(&late, lx);
    CHECK(mb_make_durable(d3, lx, NULL, "late", 4, NULL));
    mb_written(s, 3, &late, true);
    char *lf = mfmt("%s/%s", d3, lx);
    CHECK(!mb_known_ref(s, 3, &late) && !exists(lf));
    mb_store_free(s);
    mb_remove_tree(d);
    free(d);
    free(d2);
    free(d3);
    free(lf);
}

static mb_live one_forgotten(void *c, mj_job_id j) {
    (void)c;
    return j == 1 ? MB_FORGOTTEN : MB_LISTED;
}

static void eviction(void) {
    for (int persisted = 1; persisted >= 0; persisted--) {
        char *d = dir("evict");
        mb_store *s = mb_open(persisted ? d : NULL, 100, NULL);
        uint8_t fill[5][30];
        for (int i = 0; i < 5; i++) memset(fill[i], i + 1, 30);
        mj_hash a, b, c, e, x;
        mb_reserved r;
        CHECK(put(s, 1, fill[0], 30, one_forgotten, &a, &r, NULL));
        mb_reserved_free(&r);
        CHECK(put(s, 2, fill[1], 30, one_forgotten, &b, &r, NULL));
        mb_reserved_free(&r);
        CHECK(put(s, 3, fill[2], 30, one_forgotten, &c, &r, NULL));
        mb_reserved_free(&r);
        /* 90 of 100: 30 more evicts the forgotten job's. */
        CHECK(put(s, 4, fill[3], 30, one_forgotten, &e, &r, NULL));
        CHECK(r.evicted.n == 1 && r.evicted.v[0].job == 1 && mj_hash_eq(&r.evicted.v[0].h, &a));
        mb_reserved_free(&r);
        CHECK(!mb_has(s, &a) && mb_has(s, &b) && mb_has(s, &c) && mb_has(s, &e));
        /* Then nothing is evictable: refused, nothing evicted. */
        char *err = NULL;
        CHECK(!put(s, 5, fill[4], 30, one_forgotten, &x, &r, &err) && err && strstr(err, "full"));
        free(err);
        CHECK(mb_has(s, &b) && mb_has(s, &c) && mb_has(s, &e) && mb_bytes(s) == 90);
        /* The same bytes for another job: a reference, no room needed. */
        mj_hash b2;
        CHECK(put(s, 5, fill[1], 30, one_forgotten, &b2, &r, NULL));
        CHECK(mj_hash_eq(&b2, &b) && r.evicted.n == 0 && mb_has_ref(s, 5, &b) && mb_bytes(s) == 90);
        mb_reserved_free(&r);
        if (persisted) {
            char *d1 = mfmt("%s/0.1", d), *d4 = mfmt("%s/0.4", d);
            CHECK(!exists(d1) && exists(d4));
            free(d1);
            free(d4);
        }
        mb_refs g = mb_drop_job(s, 2);
        CHECK(g.n == 1 && mj_hash_eq(&g.v[0].h, &b));
        mb_refs_free(&g);
        CHECK(mb_has_ref(s, 5, &b) && mb_bytes(s) == 90);
        g = mb_drop_job(s, 4);
        CHECK_U(g.n, 1);
        mb_refs_free(&g);
        CHECK_U(mb_bytes(s), 60);
        mb_store_free(s);
        mb_remove_tree(d);
        free(d);
    }
}

/* The index under churn: thousands of blobs over many jobs, jobs dropped at random, every blob's presence
 * and the byte count checked against a plain list (deletions shift probe runs back). */
static void churn(void) {
    mb_store *s = mb_open(NULL, (uint64_t)1 << 40, NULL);
    enum { N = 3000 };
    static mj_hash hs[N];
    static mj_job_id job[N];
    static bool alive[N];
    uint64_t r = 0x2545f4914f6cdd1du;
    for (int i = 0; i < N; i++) {
        uint8_t b[16];
        memcpy(b, &i, sizeof i);
        memset(b + sizeof i, 7, sizeof b - sizeof i);
        hs[i] = sha(b, sizeof b);
        r ^= r << 13;
        r ^= r >> 7;
        r ^= r << 17;
        job[i] = r % 40;
        mb_reserved rv;
        CHECK(mb_reserve(s, job[i], hs[i], 16, listed, NULL, &rv, NULL));
        mb_reserved_free(&rv);
        mb_put_mem(s, job[i], hs[i], b, sizeof b);
        alive[i] = true;
    }
    for (mj_job_id j = 0; j < 40; j += 3) {
        mb_refs g = mb_drop_job(s, j);
        mb_refs_free(&g);
        for (int i = 0; i < N; i++)
            if (job[i] == j) alive[i] = false;
    }
    uint64_t want = 0;
    for (int i = 0; i < N; i++) {
        CHECK(mb_has(s, &hs[i]) == alive[i]);
        want += alive[i] ? 16 : 0;
    }
    CHECK_U(mb_bytes(s), want);
    mb_store_free(s);
}

int main(void) {
    churn();
    write_rename_durability();
    hash_verification();
    two_jobs();
    eviction();
    return mesh_check_done();
}
