/* The manifest value parser, as manifest.rs's `value`, `quoted`, `int`, `pair` and `strip_comment`. */
#include "mesh/toml.h"

#include <stdlib.h>
#include <string.h>

bool mesh_toml_int(const char *s, size_t n, uint32_t *out) {
    char *b = mesh_alloc(n + 1);
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        if (s[i] != '_') b[k++] = s[i];
    bool ok = k >= 2 && b[0] == '0' && b[1] == 'x' ? mesh_parse_u32(b + 2, k - 2, 16, out) : mesh_parse_u32(b, k, 10, out);
    free(b);
    return ok;
}

bool mesh_toml_pair(const char *s, size_t n, uint32_t *a, uint32_t *b) {
    const char *colon = memchr(s, ':', n);
    if (!colon) return false;
    const char *x = s, *y = colon + 1;
    size_t xn = (size_t)(colon - s), yn = n - xn - 1;
    mesh_trim(&x, &xn);
    mesh_trim(&y, &yn);
    return mesh_toml_int(x, xn, a) && mesh_toml_int(y, yn, b);
}

/* One "..." at the start of s: its contents and where the rest starts. */
static bool quoted(const char *s, size_t n, const char **body, size_t *bn, const char **rest, size_t *rn) {
    if (n == 0 || s[0] != '"') return false;
    const char *end = memchr(s + 1, '"', n - 1);
    if (!end) return false;
    *body = s + 1;
    *bn = (size_t)(end - s - 1);
    *rest = end + 1;
    *rn = n - (size_t)(end + 1 - s);
    return true;
}

static void free_items(char **items, size_t n) {
    for (size_t i = 0; i < n; i++) free(items[i]);
    free(items);
}

bool mesh_toml_value(const char *v, size_t n, mesh_toml_val *out, char **err) {
    *out = (mesh_toml_val){0};
    mesh_trim(&v, &n);
    if ((n == 4 && memcmp(v, "true", 4) == 0) || (n == 5 && memcmp(v, "false", 5) == 0)) {
        out->kind = MESH_TOML_BOOL;
        out->b = n == 4;
        return true;
    }
    if (n > 0 && v[0] == '"') {
        const char *body, *rest;
        size_t bn, rn;
        if (!quoted(v, n, &body, &bn, &rest, &rn)) return mesh_errf(err, "unterminated string");
        const char *r = rest;
        size_t rr = rn;
        mesh_trim(&r, &rr);
        if (rr != 0) {
            mesh_text t = {0};
            mesh_text_puts(&t, "junk after string: ");
            mesh_text_debug_str(&t, rest, rn);
            if (err) { free(*err); *err = mesh_text_take(&t); } else mesh_text_free(&t);
            return false;
        }
        out->kind = MESH_TOML_STR;
        out->s = mesh_strndup(body, bn);
        return true;
    }
    if (n > 0 && v[0] == '[') {
        const char *inner = v + 1;
        size_t in = n - 1;
        if (in == 0 || inner[in - 1] != ']') return mesh_errf(err, "a list must close on its line");
        in--;
        const char *rest = inner;
        size_t rn = in;
        mesh_trim(&rest, &rn);
        char **items = NULL;
        size_t count = 0;
        while (rn > 0) {
            const char *body, *r;
            size_t bn, rr;
            if (!quoted(rest, rn, &body, &bn, &r, &rr)) {
                mesh_text t = {0};
                mesh_text_puts(&t, "list items are strings: ");
                mesh_text_debug_str(&t, rest, rn);
                if (err) { free(*err); *err = mesh_text_take(&t); } else mesh_text_free(&t);
                free_items(items, count);
                return false;
            }
            items = mesh_realloc(items, (count + 1) * sizeof *items);
            items[count++] = mesh_strndup(body, bn);
            rest = r;
            rn = rr;
            mesh_trim(&rest, &rn);
            if (rn > 0 && rest[0] == ',') {
                rest++;
                rn--;
                mesh_trim(&rest, &rn);
            } else if (rn != 0) {
                mesh_text t = {0};
                mesh_text_puts(&t, "expected ',' in list: ");
                mesh_text_debug_str(&t, rest, rn);
                if (err) { free(*err); *err = mesh_text_take(&t); } else mesh_text_free(&t);
                free_items(items, count);
                return false;
            }
        }
        out->kind = MESH_TOML_LIST;
        out->items = items;
        out->nitems = count;
        return true;
    }
    uint32_t x;
    if (mesh_toml_int(v, n, &x)) {
        out->kind = MESH_TOML_INT;
        out->n = x;
        return true;
    }
    if (n > 0 && v[0] == '-' && mesh_toml_int(v + 1, n - 1, &x)) {
        out->kind = MESH_TOML_NEG;
        out->n = x;
        return true;
    }
    double f;
    if (mesh_parse_f64(v, n, &f) && memchr(v, '.', n) && f - f == 0) {
        out->kind = MESH_TOML_FLOAT;
        out->f = f;
        return true;
    }
    mesh_text t = {0};
    mesh_text_puts(&t, "bad value ");
    mesh_text_debug_str(&t, v, n);
    if (err) { free(*err); *err = mesh_text_take(&t); } else mesh_text_free(&t);
    return false;
}

size_t mesh_toml_strip_comment(const char *line, size_t n) {
    bool in_str = false;
    for (size_t i = 0; i < n; i++) {
        if (line[i] == '"') in_str = !in_str;
        else if (line[i] == '#' && !in_str) return i;
    }
    return n;
}

void mesh_toml_val_debug(mesh_text *t, const mesh_toml_val *v) {
    switch (v->kind) {
    case MESH_TOML_INT: mesh_text_printf(t, "Int(%u)", (unsigned)v->n); break;
    case MESH_TOML_NEG: mesh_text_printf(t, "Neg(%u)", (unsigned)v->n); break;
    case MESH_TOML_FLOAT:
        mesh_text_puts(t, "Float(");
        mesh_text_debug_f64(t, v->f);
        mesh_text_putc(t, ')');
        break;
    case MESH_TOML_STR:
        mesh_text_puts(t, "Str(");
        mesh_text_debug_str(t, v->s, strlen(v->s));
        mesh_text_putc(t, ')');
        break;
    case MESH_TOML_BOOL: mesh_text_puts(t, v->b ? "Bool(true)" : "Bool(false)"); break;
    case MESH_TOML_LIST:
        mesh_text_puts(t, "List([");
        for (size_t i = 0; i < v->nitems; i++) {
            if (i) mesh_text_puts(t, ", ");
            mesh_text_debug_str(t, v->items[i], strlen(v->items[i]));
        }
        mesh_text_puts(t, "])");
        break;
    }
}

char *mesh_toml_val_debug_str(const mesh_toml_val *v) {
    mesh_text t = {0};
    mesh_toml_val_debug(&t, v);
    return mesh_text_take(&t);
}

void mesh_toml_val_free(mesh_toml_val *v) {
    free(v->s);
    free_items(v->items, v->nitems);
    *v = (mesh_toml_val){0};
}
