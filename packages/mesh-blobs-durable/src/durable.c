/* blobs.rs's make_durable, write_durable, sync_dir and fsync, with POSIX calls. */
#include "mesh/blobs/durable.h"
#include "mesh/strbuf.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail(char **err, const char *what, const char *path) {
    if (err) *err = mfmt("%s %s: %s", what, path, strerror(errno));
    return false;
}

bool mb_mkdirs(const char *dir, char **err) {
    char *p = mstrdup(dir);
    for (char *s = p + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0777) != 0 && errno != EEXIST) {
            bool r = fail(err, "mkdir", p);
            free(p);
            return r;
        }
        *s = '/';
    }
    bool ok = mkdir(p, 0777) == 0 || errno == EEXIST || fail(err, "mkdir", p);
    free(p);
    return ok;
}

bool mb_write_durable(const char *dir, const char *name, const void *bytes, size_t n, char **err) {
    if (!mb_mkdirs(dir, err)) return false;
    char *tmp = mfmt("%s/%s.tmp", dir, name), *dst = mfmt("%s/%s", dir, name);
    bool ok = false;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) fail(err, "create", tmp);
    else {
        const char *p = bytes;
        size_t left = n;
        while (left) {
            ssize_t w = write(fd, p, left);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            p += w;
            left -= (size_t)w;
        }
        if (left) fail(err, "write", tmp);
        else if (fsync(fd) != 0) fail(err, "fsync", tmp);
        else ok = true;
        close(fd);
        if (ok && rename(tmp, dst) != 0) ok = fail(err, "rename", tmp);
    }
    free(tmp);
    free(dst);
    return ok;
}

bool mb_make_durable(const char *dir, const char *name, const char *link_from, const void *bytes, size_t n, char **err) {
    if (link_from) {
        if (!mb_mkdirs(dir, err)) return false;
        char *dst = mfmt("%s/%s", dir, name);
        int r = link(link_from, dst);
        if (r == 0) {
            free(dst);
            return true;
        }
        if (errno == EEXIST) unlink(dst);
        free(dst);
    }
    return mb_write_durable(dir, name, bytes, n, err);
}

bool mb_sync_dir(const char *dir, char **err) {
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return fail(err, "open", dir);
    bool ok = fsync(fd) == 0 || fail(err, "fsync", dir);
    close(fd);
    return ok;
}

void mb_remove_tree(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        unlink(dir);
        return;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char *p = mfmt("%s/%s", dir, e->d_name);
        struct stat st;
        if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) mb_remove_tree(p);
        else unlink(p);
        free(p);
    }
    closedir(d);
    rmdir(dir);
}
