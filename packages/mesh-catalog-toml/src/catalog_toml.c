/* Line probes of program.toml, one per grep/sed of build-programs.sh. */
#include "mesh/catalog_toml.h"

#include <stdint.h>
#include <string.h>

/* Calls f on each line (without its newline) until it returns true. */
static bool each_line(const char *t, bool (*f)(const char *l, size_t n, void *u), void *u) {
    while (*t) {
        const char *e = strchr(t, '\n');
        size_t n = e ? (size_t)(e - t) : strlen(t);
        if (f(t, n, u)) return true;
        if (!e) break;
        t = e + 1;
    }
    return false;
}

static size_t skip_spaces(const char *l, size_t i, size_t n) {
    while (i < n && l[i] == ' ') i++;
    return i;
}

typedef struct {
    char *out;
    size_t cap;
} name_q;

static bool name_line(const char *l, size_t n, void *u) {
    name_q *q = u;
    if (n < 4 || memcmp(l, "name", 4) != 0) return false;
    size_t i = skip_spaces(l, 4, n);
    if (i >= n || l[i] != '=') return false;
    i = skip_spaces(l, i + 1, n);
    if (i >= n || l[i] != '"') return false;
    /* sed's greedy .* up to the last '"' of the line, which must end it. */
    if (l[n - 1] != '"' || n - 1 <= i) return false;
    size_t len = n - 1 - (i + 1);
    if (len + 1 > q->cap) return false;
    memcpy(q->out, l + i + 1, len);
    q->out[len] = 0;
    return true;
}

bool mesh_catalog_toml_name(const char *text, char *out, size_t cap) {
    name_q q = {out, cap};
    return each_line(text, name_line, &q);
}

static bool starts(const char *l, size_t n, const char *p) {
    size_t k = strlen(p);
    return n >= k && memcmp(l, p, k) == 0;
}

static bool backends_line(const char *l, size_t n, void *u) {
    (void)u;
    return starts(l, n, "backends");
}
bool mesh_catalog_toml_has_backends(const char *text) { return each_line(text, backends_line, NULL); }

/* '^backends *= *\[ *"native" *\]' (the line may go on after the ']'). */
static bool native_line(const char *l, size_t n, void *u) {
    (void)u;
    if (!starts(l, n, "backends")) return false;
    size_t i = skip_spaces(l, 8, n);
    if (i >= n || l[i] != '=') return false;
    i = skip_spaces(l, i + 1, n);
    if (i >= n || l[i] != '[') return false;
    i = skip_spaces(l, i + 1, n);
    if (n - i < 8 || memcmp(l + i, "\"native\"", 8) != 0) return false;
    i = skip_spaces(l, i + 8, n);
    return i < n && l[i] == ']';
}
bool mesh_catalog_toml_native(const char *text) { return each_line(text, native_line, NULL); }

static bool backend_line(const char *l, size_t n, void *u) {
    const char *want = u;
    if (!starts(l, n, "backends")) return false;
    size_t i = skip_spaces(l, 8, n);
    if (i >= n || l[i] != '=') return false;
    size_t k = strlen(want);
    for (size_t j = i + 1; j + k + 2 <= n; j++)
        if (l[j] == '"' && memcmp(l + j + 1, want, k) == 0 && l[j + 1 + k] == '"') return true;
    return false;
}
bool mesh_catalog_toml_backend(const char *text, const char *b) { return each_line(text, backend_line, (void *)(uintptr_t)b); }

typedef struct {
    const char *key;
    char *out;
    size_t cap, len;
    bool found;
} comment_q;

static bool comment_line(const char *l, size_t n, void *u) {
    comment_q *q = u;
    size_t k = strlen(q->key);
    if (n < 4 + k || l[0] != '#' || l[1] != ' ' || memcmp(l + 2, q->key, k) != 0 || l[2 + k] != ':') return false;
    size_t i = skip_spaces(l, 3 + k, n);
    size_t len = n - i;
    if (q->found && q->len + 1 < q->cap) q->out[q->len++] = '\n';
    if (q->len + len + 1 > q->cap) len = q->cap - q->len - 1;
    memcpy(q->out + q->len, l + i, len);
    q->len += len;
    q->out[q->len] = 0;
    q->found = true;
    return false;
}

bool mesh_catalog_toml_comment(const char *text, const char *key, char *out, size_t cap) {
    if (cap == 0) return false;
    comment_q q = {key, out, cap, 0, false};
    out[0] = 0;
    each_line(text, comment_line, &q);
    return q.found;
}
