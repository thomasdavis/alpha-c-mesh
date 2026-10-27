/* loader.rs: catalog names, confinement under --programs, reading a program, hashes. */
#include "mesh/loader.h"

#include "mesh/abi/fnv.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Rust's io::Error Display. */
static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    mesh_text t = {0};
    char buf[65536];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) mesh_text_putn(&t, buf, k);
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        mesh_text_free(&t);
        errno = EIO;
        return NULL;
    }
    *n = t.len;
    return mesh_text_take(&t);
}

void mesh_payload_copy(mesh_payload *dst, const mesh_payload *src) {
    dst->dir = mesh_strdup(src->dir);
    dst->manifest = mesh_strdup(src->manifest);
    dst->ptx_len = src->ptx_len;
    dst->ptx = src->ptx_len ? mesh_alloc(src->ptx_len) : NULL;
    if (src->ptx_len) memcpy(dst->ptx, src->ptx, src->ptx_len);
}

void mesh_payload_free(mesh_payload *p) {
    free(p->dir);
    free(p->manifest);
    free(p->ptx);
    *p = (mesh_payload){0};
}

char *mesh_catalog_name(const char *spec, char **err) {
    size_t n = strlen(spec);
    static const char suffix[] = "/program.toml";
    if (n >= sizeof suffix - 1 && strcmp(spec + n - (sizeof suffix - 1), suffix) == 0) n -= sizeof suffix - 1;
    while (n > 0 && spec[n - 1] == '/') n--;
    bool ok = n > 0 && n <= 64;
    for (size_t i = 0; ok && i < n; i++) ok = (spec[i] >= 'a' && spec[i] <= 'z') || (spec[i] >= '0' && spec[i] <= '9') || spec[i] == '-';
    if (ok) return mesh_strndup(spec, n);
    char *q = mesh_debug_str(spec);
    mesh_errf(err, "manifest: %s: programs are named [a-z0-9-] under --programs (no paths)", q);
    free(q);
    return NULL;
}

/* Rust's Path::starts_with on canonical paths: whole components. */
static bool path_within(const char *dir, const char *base) {
    size_t n = strlen(base);
    if (strncmp(dir, base, n) != 0) return false;
    return dir[n] == 0 || dir[n] == '/' || (n > 0 && base[n - 1] == '/');
}

char *mesh_loader_resolve(const char *root, const char *spec, char **err) {
    char *name = mesh_catalog_name(spec, err);
    if (!name) return NULL;
    char base[PATH_MAX], dir[PATH_MAX];
    if (!realpath(root, base)) {
        char *e = os_error(errno);
        mesh_errf(err, "manifest: --programs %s: %s", root, e);
        free(e);
        free(name);
        return NULL;
    }
    char *joined = mesh_fmt("%s/%s", root, name);
    if (!realpath(joined, dir)) {
        char *e = os_error(errno);
        mesh_errf(err, "manifest: %s: %s", name, e);
        free(e);
        free(joined);
        free(name);
        return NULL;
    }
    free(joined);
    struct stat st;
    if (!path_within(dir, base) || stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        mesh_errf(err, "manifest: %s: not a program dir under --programs", name);
        free(name);
        return NULL;
    }
    free(name);
    return mesh_strdup(dir);
}

bool mesh_loader_read(const char *root, const char *spec, bool need_ptx, mesh_payload *out, char **err) {
    *out = (mesh_payload){0};
    char *dir = mesh_loader_resolve(root, spec, err);
    if (!dir) return false;
    mesh_manifest m;
    char *ptx_path = NULL, *why = NULL;
    if (!mesh_manifest_load(dir, &m, &ptx_path, &why)) {
        mesh_errf(err, "manifest: %s", why);
        free(why);
        free(dir);
        return false;
    }
    char *toml = mesh_fmt("%s/program.toml", dir);
    size_t tn;
    char *text = slurp(toml, &tn);
    free(toml);
    free(dir);
    if (!text) {
        char *e = os_error(errno);
        mesh_errf(err, "manifest: %s", e);
        free(e);
        goto fail;
    }
    /* A native program is an executable per target: no PTX. */
    need_ptx = need_ptx && !mesh_manifest_native(&m);
    size_t pn = 0;
    char *ptx = slurp(ptx_path, &pn);
    if (!ptx) {
        int e0 = errno;
        if (need_ptx || e0 != ENOENT) {
            char *e = os_error(e0);
            mesh_errf(err, "manifest: %s (%s): %s", ptx_path, m.name, e);
            free(e);
            free(text);
            goto fail;
        }
        pn = 0;
    }
    out->dir = mesh_catalog_name(spec, NULL);
    out->manifest = text;
    out->ptx = (uint8_t *)ptx;
    out->ptx_len = pn;
    if (pn == 0) {
        free(out->ptx);
        out->ptx = NULL;
    }
    free(ptx_path);
    mesh_manifest_free(&m);
    return true;
fail:
    free(ptx_path);
    mesh_manifest_free(&m);
    return false;
}

uint64_t mesh_image_hash(const uint8_t *code, size_t n, uint32_t param) {
    if (param == 0) return mesh_fnv1a64(code, n);
    uint8_t *b = mesh_alloc(n + 11);
    if (n) memcpy(b, code, n);
    memcpy(b + n, "\0param=", 7);
    for (int i = 0; i < 4; i++) b[n + 7 + (size_t)i] = (uint8_t)(param >> (8 * i));
    uint64_t h = mesh_fnv1a64(b, n + 11);
    free(b);
    return h;
}

uint64_t mesh_native_identity(const char *manifest, const char *source, uint32_t param) {
    const char *s = source;
    size_t sn = strlen(source);
    mesh_trim(&s, &sn);
    mesh_text b = {0};
    mesh_text_puts(&b, manifest);
    mesh_text_putn(&b, "\0source=", 8);
    mesh_text_putn(&b, s, sn);
    uint64_t h = mesh_image_hash((const uint8_t *)mesh_text_str(&b), b.len, param);
    mesh_text_free(&b);
    return h;
}

bool mesh_loader_identity(const char *root, const char *spec, mesh_payload *p, mesh_manifest *m, uint64_t *hash, char **err) {
    if (!mesh_loader_read(root, spec, false, p, err)) return false;
    char *why = NULL;
    if (!mesh_manifest_parse(p->manifest, strlen(p->manifest), m, &why)) {
        mesh_errf(err, "manifest: %s: %s", spec, why);
        free(why);
        mesh_payload_free(p);
        return false;
    }
    if (p->ptx_len) {
        *hash = mesh_image_hash(p->ptx, p->ptx_len, m->param);
        return true;
    }
    char *dir = mesh_loader_resolve(root, spec, err);
    if (!dir) {
        mesh_manifest_free(m);
        mesh_payload_free(p);
        return false;
    }
    for (size_t i = 0; i < m->nbackends; i++) {
        const char *b = m->backends[i];
        char *path = NULL;
        if (strcmp(b, "native") == 0) path = mesh_fmt("%s/source.hash", dir);
        else if (strcmp(b, "wasm") == 0) path = mesh_fmt("%s/%s.wasm", dir, m->name);
        else if (strcmp(b, "metal") == 0) path = mesh_fmt("%s/program.metal", dir);
        if (!path) continue;
        size_t n;
        char *bytes = slurp(path, &n);
        free(path);
        if (!bytes) continue;
        *hash = strcmp(b, "native") == 0 ? mesh_native_identity(p->manifest, bytes, m->param)
                                          : mesh_image_hash((const uint8_t *)bytes, n, m->param);
        free(bytes);
        free(dir);
        return true;
    }
    free(dir);
    mesh_text t = {0};
    for (size_t i = 0; i < m->nbackends; i++) mesh_text_printf(&t, "%s%s", i ? ", " : "", m->backends[i]);
    mesh_errf(err, "manifest: %s: no image in the catalog dir for any of its backends (%s)", spec, mesh_text_str(&t));
    mesh_text_free(&t);
    mesh_manifest_free(m);
    mesh_payload_free(p);
    return false;
}

bool mesh_check_hash(const char *name, const char *what, uint64_t hash, const uint64_t *expect, char **err) {
    if (expect && *expect != hash)
        return mesh_errf(err, "peer: %s: the %s received hashes to %016llx, the peer announced %016llx", name, what,
                         (unsigned long long)hash, (unsigned long long)*expect);
    return true;
}

static int by_name(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

bool mesh_catalog(const char *root, char ***names, size_t *n, char **err) {
    *names = NULL;
    *n = 0;
    DIR *d = opendir(root);
    if (!d) {
        char *e = os_error(errno);
        mesh_errf(err, "--programs %s: %s", root, e);
        free(e);
        return false;
    }
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char *toml = mesh_fmt("%s/%s/program.toml", root, ent->d_name);
        struct stat st;
        bool file = stat(toml, &st) == 0 && S_ISREG(st.st_mode);
        free(toml);
        if (!file) continue;
        char *name = mesh_catalog_name(ent->d_name, NULL);
        if (!name) continue;
        /* catalog_name accepts a trailing "/program.toml"; a dir entry is the name itself. */
        if (strcmp(name, ent->d_name) != 0) {
            free(name);
            continue;
        }
        *names = mesh_realloc(*names, (*n + 1) * sizeof **names);
        (*names)[(*n)++] = name;
    }
    closedir(d);
    qsort(*names, *n, sizeof **names, by_name);
    return true;
}
