/* loader.rs's read_state and write_state. */
#include "mesh/state_file.h"
#include "mesh/loader.h"
#include "mesh/text.h"

#include "mesh/abi/name.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *os_error(int e) { return mesh_fmt("%s (os error %d)", strerror(e), e); }

void mesh_state_free(mesh_saved *set, size_t n) {
    for (size_t i = 0; i < n; i++) {
        free(set[i].name);
        free(set[i].dir);
    }
    free(set);
}

/* Splits on Rust's whitespace; up to max words; returns the count (more than max: max + 1). */
static size_t words(const char *s, size_t n, const char **w, size_t *wn, size_t max) {
    size_t count = 0;
    for (;;) {
        mesh_trim_start(&s, &n);
        if (n == 0) return count;
        size_t k = 0;
        while (k < n && mesh_utf8_ws_len(s + k, n - k) == 0) k++;
        if (count < max) {
            w[count] = s;
            wn[count] = k;
        }
        count++;
        if (count > max) return count;
        s += k;
        n -= k;
    }
}

bool mesh_state_read(const char *path, mesh_saved **set, size_t *n, char **err) {
    *set = NULL;
    *n = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT) return true;
        char *e = os_error(errno);
        mesh_errf(err, "--state %s: %s", path, e);
        free(e);
        return false;
    }
    mesh_text t = {0};
    char buf[8192];
    size_t k;
    while ((k = fread(buf, 1, sizeof buf, f)) > 0) mesh_text_putn(&t, buf, k);
    fclose(f);
    const char *text = mesh_text_str(&t);
    size_t len = t.len, i = 0, lineno = 0;
    bool ok = true;
    while (i < len && ok) {
        const char *line = text + i;
        const char *nl = memchr(line, '\n', len - i);
        size_t ln = nl ? (size_t)(nl - line) : len - i;
        i += ln + (nl ? 1 : 0);
        if (ln > 0 && line[ln - 1] == '\r') ln--;
        lineno++;
        const char *l = line;
        mesh_trim(&l, &ln);
        if (ln == 0 || l[0] == '#') continue;
        const char *w[3];
        size_t wn[3];
        size_t c = words(l, ln, w, wn, 3);
        bool good = c == 3;
        char *name = NULL, *dir = NULL;
        if (good) {
            name = mesh_strndup(w[0], wn[0]);
            dir = mesh_strndup(w[1], wn[1]);
            char *cn = mesh_catalog_name(dir, NULL);
            good = mesh_valid_name((const uint8_t *)name, strlen(name)) && cn && wn[2] == 1 && (w[2][0] == '0' || w[2][0] == '1');
            free(cn);
        }
        if (!good) {
            free(name);
            free(dir);
            mesh_text q = {0};
            mesh_text_debug_str(&q, l, ln);
            mesh_errf(err, "--state %s line %zu: expected `name dir 0|1`, got %s", path, lineno, mesh_text_str(&q));
            mesh_text_free(&q);
            ok = false;
            break;
        }
        *set = mesh_realloc(*set, (*n + 1) * sizeof **set);
        (*set)[(*n)++] = (mesh_saved){name, dir, w[2][0] == '1'};
    }
    mesh_text_free(&t);
    if (!ok) {
        mesh_state_free(*set, *n);
        *set = NULL;
        *n = 0;
    }
    return ok;
}

bool mesh_state_write(const char *path, const mesh_saved *set, size_t n, char **err) {
    mesh_text s = {0};
    mesh_text_puts(&s, "# meshd --state: loaded programs, in slot order (name, dir under --programs, running)\n");
    for (size_t i = 0; i < n; i++) mesh_text_printf(&s, "%s %s %u\n", set[i].name, set[i].dir, set[i].running ? 1u : 0u);
    /* Rust's path.with_extension("tmp"): the extension replaced, or added. */
    const char *slash = strrchr(path, '/');
    const char *base = slash ? slash + 1 : path;
    const char *dot = strrchr(base, '.');
    char *tmp = dot && dot != base ? mesh_fmt("%.*s.tmp", (int)(dot - path), path) : mesh_fmt("%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    bool ok = f && fwrite(mesh_text_str(&s), 1, s.len, f) == s.len;
    int e0 = errno;
    if (f && fclose(f) != 0 && ok) {
        ok = false;
        e0 = errno;
    }
    if (ok && rename(tmp, path) != 0) {
        ok = false;
        e0 = errno;
    }
    if (!ok) {
        char *e = os_error(e0);
        mesh_errf(err, "--state %s: %s", path, e);
        free(e);
    }
    free(tmp);
    mesh_text_free(&s);
    return ok;
}
