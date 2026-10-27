/* The file list (find), its order (LC_ALL=C sort), and the hash of "path\0content" each. */
#include "mesh/source_hash.h"
#include "sha256_stream.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    char **v;
    size_t n, cap;
} list;

static void add(list *l, const char *s) {
    if (l->n == l->cap) {
        l->cap = l->cap ? 2 * l->cap : 64;
        char **nv = realloc(l->v, l->cap * sizeof *nv);
        if (!nv) abort();
        l->v = nv;
    }
    size_t k = strlen(s) + 1;
    l->v[l->n] = malloc(k);
    if (!l->v[l->n]) abort();
    memcpy(l->v[l->n++], s, k);
}

static bool source_name(const char *n) {
    size_t k = strlen(n);
    return (k > 2 && (strcmp(n + k - 2, ".c") == 0 || strcmp(n + k - 2, ".h") == 0)) || strcmp(n, "sexc.json") == 0 ||
           strcmp(n, "program.toml") == 0;
}

/* rel is relative to root; recurse into folders except .sexc. */
static void walk(const char *root, const char *rel, list *out) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    DIR *d = opendir(path);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0 || strcmp(e->d_name, ".sexc") == 0) continue;
        char r[4096], full[8192];
        snprintf(r, sizeof r, "%s/%s", rel, e->d_name);
        snprintf(full, sizeof full, "%s/%s", root, r);
        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walk(root, r, out);
        else if (S_ISREG(st.st_mode) && source_name(e->d_name)) add(out, r);
    }
    closedir(d);
}

static int by_bytes(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

bool mesh_source_hash(const char *root, const char *const *dirs, size_t ndirs, char out[17]) {
    list files = {0};
    for (size_t i = 0; i < ndirs; i++) walk(root, dirs[i], &files);
    if (files.n) qsort(files.v, files.n, sizeof *files.v, by_bytes);
    sha256_stream s;
    sha256_stream_init(&s);
    bool ok = true;
    for (size_t i = 0; i < files.n && ok; i++) {
        sha256_stream_update(&s, files.v[i], strlen(files.v[i]) + 1);
        char full[8192];
        snprintf(full, sizeof full, "%s/%s", root, files.v[i]);
        FILE *f = fopen(full, "rb");
        if (!f) { ok = false; break; }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) sha256_stream_update(&s, buf, n);
        ok = !ferror(f);
        fclose(f);
    }
    for (size_t i = 0; i < files.n; i++) free(files.v[i]);
    free(files.v);
    uint8_t d[32];
    char hex[65];
    sha256_stream_final(&s, d);
    sha256_stream_hex(d, hex);
    memcpy(out, hex, 16);
    out[16] = 0;
    return ok;
}
