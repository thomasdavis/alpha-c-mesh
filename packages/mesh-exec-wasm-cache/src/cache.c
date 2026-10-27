/* The cache directory: names, atomic writes, and eviction oldest first. */
#include "mesh/exec_wasm_cache.h"

#include "mesh/abi/fnv.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *fmt_err(const char *a, const char *b) {
    size_t n = strlen(a) + strlen(b) + 3;
    char *s = malloc(n);
    if (s) snprintf(s, n, "%s: %s", a, b);
    return s;
}

char *mesh_wasm_cache_path(const char *dir, const uint8_t *wasm, size_t n, uint64_t engine_hash) {
    size_t len = strlen(dir) + 1 + 16 + 1 + 16 + 6 + 1;
    char *p = malloc(len);
    if (p)
        snprintf(p, len, "%s/%016llx-%016llx.cwasm", dir, (unsigned long long)mesh_fnv1a64(wasm, n),
                 (unsigned long long)engine_hash);
    return p;
}

bool mesh_wasm_cache_dir(const char *dir, char **err) {
    char *path = strdup(dir);
    if (!path) return false;
    for (char *p = path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(path, 0755) != 0 && errno != EEXIST) break;
        *p = '/';
    }
    bool ok = mkdir(path, 0755) == 0 || errno == EEXIST;
    struct stat st;
    if (ok && (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {
        errno = ENOTDIR;
        ok = false;
    }
    if (!ok && err) {
        char what[4096];
        snprintf(what, sizeof what, "--wasm-cache %s", dir);
        *err = fmt_err(what, strerror(errno));
    }
    free(path);
    return ok;
}

bool mesh_wasm_cache_store(const char *path, const uint8_t *bytes, size_t n, char **err) {
    size_t len = strlen(path) + 5;
    char *tmp = malloc(len);
    if (!tmp) return false;
    /* path.with_extension("tmp"): foo.cwasm -> foo.tmp */
    snprintf(tmp, len, "%s", path);
    char *dot = strrchr(tmp, '.');
    char *slash = strrchr(tmp, '/');
    if (dot && (!slash || dot > slash)) strcpy(dot, ".tmp");
    else strcat(tmp, ".tmp");
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(bytes, 1, n, f) == n;
    if (f && fclose(f) != 0) ok = false;
    if (ok && rename(tmp, path) != 0) ok = false;
    if (!ok) {
        if (err) *err = fmt_err(path, strerror(errno));
        remove(tmp);
    }
    free(tmp);
    return ok;
}

bool mesh_wasm_cache_load(const char *path, uint8_t **out, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t *b = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            cap = cap ? cap * 2 : 65536;
            uint8_t *q = realloc(b, cap);
            if (!q) {
                free(b);
                fclose(f);
                return false;
            }
            b = q;
        }
        size_t k = fread(b + len, 1, cap - len, f);
        len += k;
        if (k == 0) break;
    }
    bool ok = !ferror(f);
    fclose(f);
    if (!ok) {
        free(b);
        return false;
    }
    *out = b;
    *n = len;
    return true;
}

typedef struct {
    struct timespec mtime;
    uint64_t len;
    char *path;
} entry;

static int older(const void *a, const void *b) {
    const entry *x = a, *y = b;
    if (x->mtime.tv_sec != y->mtime.tv_sec) return x->mtime.tv_sec < y->mtime.tv_sec ? -1 : 1;
    if (x->mtime.tv_nsec != y->mtime.tv_nsec) return x->mtime.tv_nsec < y->mtime.tv_nsec ? -1 : 1;
    if (x->len != y->len) return x->len < y->len ? -1 : 1;
    return strcmp(x->path, y->path);
}

size_t mesh_wasm_cache_evict(const char *dir, uint64_t cap, void (*on_evict)(const char *path)) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    entry *files = NULL;
    size_t n = 0, ncap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        size_t len = strlen(de->d_name);
        if (len < 6 || strcmp(de->d_name + len - 6, ".cwasm") != 0) continue;
        char *path = malloc(strlen(dir) + len + 2);
        if (!path) break;
        sprintf(path, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            free(path);
            continue;
        }
        if (n == ncap) {
            ncap = ncap ? ncap * 2 : 16;
            entry *q = realloc(files, ncap * sizeof *q);
            if (!q) {
                free(path);
                break;
            }
            files = q;
        }
        files[n++] = (entry){st.st_mtim, (uint64_t)st.st_size, path};
    }
    closedir(d);
    qsort(files, n, sizeof *files, older);
    uint64_t total = 0;
    for (size_t i = 0; i < n; i++) total += files[i].len;
    size_t evicted = 0;
    for (size_t i = 0; i < n; i++) {
        if (total > cap && remove(files[i].path) == 0) {
            total -= files[i].len;
            evicted++;
            if (on_evict) on_evict(files[i].path);
        }
        free(files[i].path);
    }
    free(files);
    return evicted;
}
