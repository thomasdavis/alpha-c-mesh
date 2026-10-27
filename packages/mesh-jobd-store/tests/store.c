/* jobd.rs's `a_torn_write_is_cut_back` (review finding 4): a write that fails half-way is cut back to
 * the last whole record, and a torn tail found at start (a crash mid-append) is dropped; every record
 * before it replays. Also compaction and the in-memory store. */
#include "mesh/jobd/store.h"
#include "mesh/check.h"
#include "mesh/strbuf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static mj_log e(uint64_t l, uint64_t n) {
    mj_ops ops = {0};
    mj_ops_push(&ops, mj_op_dupes(1, n));
    return mj_log_entry(l, n, false, ops);
}

static uint64_t size_of(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 ? (uint64_t)st.st_size : 0;
}

static int logged;
static void logline(void *c, const char *line) {
    (void)c;
    if (strstr(line, "torn last record")) logged++;
}

int main(void) {
    char *d = mfmt("%s/meshd-jobd-torn-%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    mkdir(d, 0777);
    char *p = mfmt("%s/jobs", d);
    uint64_t l = (uint64_t)1 << 40;
    mj_replica r;
    char *err = NULL;
    mjd_store *s = mjd_store_open(p, &r, NULL, NULL, &err);
    mj_replica empty;
    mj_replica_init(&empty);
    CHECK(s && mj_replica_eq(&r, &empty));
    mj_replica_free(&r);
    mj_disk dk = mjd_store_disk(s);
    mj_log recs[3] = {mj_log_lid(l), e(l, 1), e(l, 2)};
    for (int i = 0; i < 3; i++) CHECK(dk.append(dk.ctx, &recs[i], &err));
    CHECK(dk.sync(dk.ctx, &err));
    uint64_t whole = size_of(p);
    mjd_store_tear_next(s, 7);
    mj_log e3 = e(l, 3);
    CHECK(!dk.append(dk.ctx, &e3, &err) && strstr(err, "torn"));
    free(err);
    CHECK_U(size_of(p), whole); /* cut back to the last whole record */
    mjd_store_free(s);
    s = mjd_store_open(p, &r, NULL, NULL, &err);
    CHECK(s && r.lid == l && r.npending == 2);
    mjd_store_free(s);
    /* A torn tail left by a crash: dropped at start, and the file rewritten without it. */
    msb b = {0};
    mj_log_encode(&e3, &b);
    FILE *f = fopen(p, "ab");
    fwrite(b.p, 1, 9, f);
    fclose(f);
    msb_free(&b);
    mj_replica r2;
    s = mjd_store_open(p, &r2, logline, NULL, &err);
    CHECK(s && mj_replica_eq(&r2, &r) && logged == 1);
    FILE *g = fopen(p, "rb");
    msb all = {0};
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, g)) > 0) msb_put(&all, buf, n);
    fclose(g);
    mj_replica r3;
    size_t recs_n, at;
    CHECK(mj_replay(all.p, all.n, &r3, &recs_n, &at, NULL) && at == all.n);
    mj_replica_free(&r3);
    msb_free(&all);
    /* A compaction rewrites it as the replica's own log. */
    CHECK(!mjd_store_wants_compact(s));
    CHECK(mjd_store_rewrite(s, &r2, &err));
    CHECK(mjd_store_persisted(s));
    mjd_store_free(s);
    /* In memory: nothing written, appends succeed. */
    mj_replica rm;
    s = mjd_store_open(NULL, &rm, NULL, NULL, &err);
    dk = mjd_store_disk(s);
    CHECK(!mjd_store_persisted(s) && dk.append(dk.ctx, &e3, &err) && dk.sync(dk.ctx, &err));
    mjd_store_free(s);
    mj_replica_free(&rm);
    for (int i = 0; i < 3; i++) mj_log_free(&recs[i]);
    mj_log_free(&e3);
    mj_replica_free(&r);
    mj_replica_free(&r2);
    mj_replica_free(&empty);
    unlink(p);
    char *tmp = mfmt("%s.jobs-tmp", p);
    unlink(tmp);
    rmdir(d);
    free(tmp);
    free(p);
    free(d);
    return mesh_check_done();
}
