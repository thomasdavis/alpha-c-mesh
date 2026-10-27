/* peer.rs Store. */
#include "mesh/fleet_store.h"
#include "mesh/mt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *read_all(const char *path, int *e) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        *e = errno;
        return NULL;
    }
    mt_buf b = {0};
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) mt_putn(&b, chunk, n);
    fclose(f);
    return mt_take(&b);
}

bool mfs_open(const char *path, uint32_t n, mfs_store *out, mfp_persist *p, char **err) {
    int e = 0;
    char *text = read_all(path, &e);
    mfp_persist got = {0};
    if (text) {
        char *why = NULL;
        bool ok = mfp_decode(text, n, &got, &why);
        free(text);
        if (!ok) {
            *err = mt_fmt("--fleet-state %s: %s", path, why);
            free(why);
            return false;
        }
    } else if (e != ENOENT) {
        *err = mt_fmt("--fleet-state %s: %s", path, strerror(e));
        return false;
    }
    mfs_store s = {mt_strdup(path), n};
    if (!mfs_write(&s, &got, err)) {
        mfs_close(&s);
        mfp_free(&got);
        return false;
    }
    *out = s;
    *p = got;
    return true;
}

bool mfs_write(const mfs_store *s, const mfp_persist *p, char **err) {
    /* path.with_extension("tmp") */
    const char *slash = strrchr(s->path, '/'), *dot = strrchr(s->path, '.');
    char *tmp = dot && (!slash || dot > slash + 1) ? mt_fmt("%.*s.tmp", (int)(dot - s->path), s->path) : mt_fmt("%s.tmp", s->path);
    char *text = mfp_encode(p, s->n);
    size_t len = strlen(text);
    bool ok = false;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) goto fail;
    for (size_t at = 0; at < len;) {
        ssize_t w = write(fd, text + at, len - at);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            close(fd);
            goto fail;
        }
        at += (size_t)w;
    }
    if (fsync(fd) != 0) {
        close(fd);
        goto fail;
    }
    close(fd);
    if (rename(tmp, s->path) != 0) goto fail;
    if (slash && slash != s->path) {
        char *dir = mt_strndup(s->path, (size_t)(slash - s->path));
        int dfd = open(dir, O_RDONLY);
        free(dir);
        if (dfd < 0) goto fail;
        int r = fsync(dfd);
        close(dfd);
        if (r != 0) goto fail;
    }
    ok = true;
    goto done;
fail:
    *err = mt_fmt("--fleet-state %s: %s", s->path, strerror(errno));
done:
    free(tmp);
    free(text);
    return ok;
}

void mfs_close(mfs_store *s) {
    free(s->path);
    *s = (mfs_store){0};
}
