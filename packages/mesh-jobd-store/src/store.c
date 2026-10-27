/* jobd.rs's Store. */
#include "mesh/jobd/store.h"
#include "mesh/strbuf.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct mjd_store {
    char *path;
    int fd;
    uint64_t bytes, base;
    bool dirty;
    bool tear;
    size_t tear_at;
};

static void say(mjd_log_fn log, void *ctx, char *line) {
    if (log) log(ctx, line);
    free(line);
}

/* Path::with_extension("jobs-tmp"): the file name's extension replaced (or added). */
static char *tmp_of(const char *path) {
    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    const char *dot = strrchr(name, '.');
    if (dot && dot != name) return mfmt("%.*s.jobs-tmp", (int)(dot - path), path);
    return mfmt("%s.jobs-tmp", path);
}

static bool write_all(int fd, const void *b, size_t n) {
    const char *p = b;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        n -= (size_t)w;
    }
    return true;
}

bool mjd_store_rewrite(mjd_store *s, const mj_replica *r, char **err) {
    if (!s->path) return true;
    char *tmp = tmp_of(s->path);
    mj_logs lg = mj_replica_log(r);
    msb b = {0};
    for (size_t i = 0; i < lg.n; i++) mj_log_encode(&lg.v[i], &b);
    mj_logs_free(&lg);
    const char *what = NULL;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) what = "create";
    else {
        if (!write_all(fd, b.p ? b.p : "", b.n)) what = "write";
        else if (fsync(fd) != 0) what = "fsync";
        close(fd);
    }
    if (!what && rename(tmp, s->path) != 0) what = "rename";
    if (!what) {
        char *dir = mstrdup(s->path), *slash = strrchr(dir, '/');
        if (slash && slash != dir) {
            *slash = 0;
            int dfd = open(dir, O_RDONLY);
            if (dfd < 0 || fsync(dfd) != 0) what = "fsync dir";
            if (dfd >= 0) close(dfd);
        }
        free(dir);
    }
    int nfd = -1;
    if (!what && (nfd = open(s->path, O_WRONLY | O_APPEND)) < 0) what = "open";
    free(tmp);
    if (what) {
        if (err) *err = mfmt("--jobs-state %s: %s: %s", s->path, what, strerror(errno));
        msb_free(&b);
        return false;
    }
    if (s->fd >= 0) close(s->fd);
    s->fd = nfd;
    s->bytes = s->base = b.n;
    s->dirty = false;
    msb_free(&b);
    return true;
}

mjd_store *mjd_store_open(const char *path, mj_replica *rep, mjd_log_fn log, void *ctx, char **err) {
    mjd_store *s = mxalloc(sizeof *s);
    s->fd = -1;
    mj_replica_init(rep);
    if (!path) return s;
    s->path = mstrdup(path);
    FILE *f = fopen(path, "rb");
    msb b = {0};
    if (f) {
        char buf[65536];
        size_t r;
        while ((r = fread(buf, 1, sizeof buf, f)) > 0) msb_put(&b, buf, r);
        fclose(f);
    } else if (errno != ENOENT) {
        if (err) *err = mfmt("--jobs-state %s: %s", path, strerror(errno));
        mjd_store_free(s);
        return NULL;
    }
    size_t n = 0, whole = 0;
    char *e = NULL;
    mj_replica_free(rep);
    if (!mj_replay(b.p ? b.p : "", b.n, rep, &n, &whole, &e)) {
        if (err) *err = mfmt("--jobs-state %s: %s", path, e);
        free(e);
        msb_free(&b);
        mj_replica_init(rep);
        mjd_store_free(s);
        return NULL;
    }
    if (whole < b.n) say(log, ctx, mfmt("jobs: %s: a torn last record (%zu bytes) dropped", path, b.n - whole));
    msb_free(&b);
    if (!mjd_store_rewrite(s, rep, err)) {
        mjd_store_free(s);
        return NULL;
    }
    if (n > 0)
        say(log, ctx, mfmt("jobs: %zu records of %s replayed: %zu job(s) committed, %zu entr(ies) pending", n, path, momap_len(rep->committed.jobs),
                           rep->npending));
    return s;
}

void mjd_store_free(mjd_store *s) {
    if (!s) return;
    if (s->fd >= 0) close(s->fd);
    free(s->path);
    free(s);
}

bool mjd_store_persisted(const mjd_store *s) { return s->path != NULL; }

bool mjd_store_wants_compact(const mjd_store *s) { return s->path && s->bytes > 2 * s->base + (8u << 20); }

void mjd_store_tear_next(mjd_store *s, size_t k) {
    s->tear = true;
    s->tear_at = k;
}

static bool st_append(void *ctx, const mj_log *l, char **err) {
    mjd_store *s = ctx;
    if (s->fd < 0) return true;
    msb b = {0};
    mj_log_encode(l, &b);
    bool ok;
    const char *why;
    if (s->tear) {
        s->tear = false;
        write_all(s->fd, b.p, s->tear_at < b.n ? s->tear_at : b.n);
        ok = false;
        why = "injected: torn write";
    } else {
        ok = write_all(s->fd, b.p, b.n);
        why = strerror(errno);
    }
    if (!ok) {
        /* Cut back to the last whole record (what a replay reads), then fail. */
        char *w = mstrdup(why);
        bool cut = ftruncate(s->fd, (off_t)s->bytes) == 0 && fsync(s->fd) == 0;
        if (err) *err = cut ? mfmt("job log: %s", w) : mfmt("job log: %s (and cutting it back to its last whole record: %s)", w, strerror(errno));
        free(w);
        msb_free(&b);
        return false;
    }
    s->bytes += b.n;
    s->dirty = true;
    msb_free(&b);
    return true;
}

static bool st_sync(void *ctx, char **err) {
    mjd_store *s = ctx;
    if (!s->dirty) return true;
    s->dirty = false;
    if (s->fd < 0) return true;
    /* fsync(2): the data reached the drive (not a drive cache flush, which the jobs thread can't afford every tick). */
    if (fsync(s->fd) != 0) {
        if (err) *err = mfmt("job log sync: %s", strerror(errno));
        return false;
    }
    return true;
}

mj_disk mjd_store_disk(mjd_store *s) { return (mj_disk){s, st_append, st_sync}; }
