/* View records: building, the text and JSON forms, parsing. */
#include "mesh/views.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const char *const MESH_VIEWS_STRING_KEYS[37] = {
    "job", "hash", "out", "host", "name", "program", "programs", "at", "pin", "up", "suspect", "cordon", "busy_members",
    "members", "fed_by", "consumers", "consumer", "producer", "type", "exec", "gpu", "arch", "role", "state", "verdict",
    "mode", "majority", "moving", "reserved", "migrate", "backends", "replicas", "items", "group", "reconcile", "on_loss",
    "queued",
};

mesh_rec *mesh_recs_add(mesh_recs *rs, const char *kind) {
    if (rs->len == rs->cap) {
        rs->cap = rs->cap ? 2 * rs->cap : 8;
        rs->items = mesh_xrealloc(rs->items, rs->cap * sizeof *rs->items);
    }
    mesh_rec *r = &rs->items[rs->len++];
    memset(r, 0, sizeof *r);
    r->kind = mesh_xstrdup(kind);
    r->text = mesh_xstrdup("");
    return r;
}

static void rec_free(mesh_rec *r) {
    free(r->kind);
    for (size_t i = 0; i < r->nkv; i++) { free(r->kv[i].k); free(r->kv[i].v); }
    free(r->kv);
    free(r->text);
}

void mesh_recs_free(mesh_recs *rs) {
    for (size_t i = 0; i < rs->len; i++) rec_free(&rs->items[i]);
    free(rs->items);
    rs->items = NULL;
    rs->len = rs->cap = 0;
}

static void push_raw(mesh_rec *r, const char *k, char *v) {
    if (r->nkv == r->cap) {
        r->cap = r->cap ? 2 * r->cap : 8;
        r->kv = mesh_xrealloc(r->kv, r->cap * sizeof *r->kv);
    }
    r->kv[r->nkv].k = mesh_xstrdup(k);
    r->kv[r->nkv].v = v;
    r->nkv++;
}

void mesh_recs_extend(mesh_recs *dst, const mesh_recs *src) {
    for (size_t i = 0; i < src->len; i++) {
        const mesh_rec *s = &src->items[i];
        mesh_rec *d = mesh_recs_add(dst, s->kind);
        for (size_t j = 0; j < s->nkv; j++) push_raw(d, s->kv[j].k, mesh_xstrdup(s->kv[j].v));
        free(d->text);
        d->text = mesh_xstrdup(s->text);
    }
}

/* The UTF-8 sequence at s: its code point and length (an invalid byte is itself, length 1). */
static unsigned utf8(const unsigned char *s, size_t *n) {
    if (s[0] < 0x80) { *n = 1; return s[0]; }
    if ((s[0] & 0xe0) == 0xc0 && (s[1] & 0xc0) == 0x80) { *n = 2; return (s[0] & 0x1fu) << 6 | (s[1] & 0x3fu); }
    if ((s[0] & 0xf0) == 0xe0 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80) {
        *n = 3;
        return (s[0] & 0x0fu) << 12 | (s[1] & 0x3fu) << 6 | (s[2] & 0x3fu);
    }
    *n = 1;
    return s[0];
}

/* char::is_whitespace: the Unicode White_Space property. */
static bool whitespace(unsigned c) {
    return (c >= 9 && c <= 13) || c == ' ' || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) ||
           c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

void mesh_rec_kv(mesh_rec *r, const char *k, const char *v) {
    mesh_buf b = MESH_BUF_INIT;
    if (!*v) mesh_buf_putc(&b, '-');
    for (const unsigned char *p = (const unsigned char *)v; *p;) {
        size_t n;
        unsigned c = utf8(p, &n);
        if (whitespace(c) || c == '=') mesh_buf_putc(&b, '_');
        else mesh_buf_put(&b, p, n);
        p += n;
    }
    push_raw(r, k, mesh_buf_take(&b));
}

void mesh_rec_kvf(mesh_rec *r, const char *k, const char *fmt, ...) {
    mesh_buf b = MESH_BUF_INIT;
    va_list ap;
    va_start(ap, fmt);
    mesh_buf_vprintf(&b, fmt, ap);
    va_end(ap);
    mesh_rec_kv(r, k, mesh_buf_str(&b));
    mesh_buf_free(&b);
}

void mesh_rec_kvu(mesh_rec *r, const char *k, uint64_t v) { mesh_rec_kvf(r, k, "%llu", (unsigned long long)v); }
void mesh_rec_kvi(mesh_rec *r, const char *k, int64_t v) { mesh_rec_kvf(r, k, "%lld", (long long)v); }
void mesh_rec_none(mesh_rec *r, const char *k) { mesh_rec_kv(r, k, "-"); }

void mesh_rec_f(mesh_rec *r, const char *k, bool some, double v, int p) {
    if (!some || v != v || v - v != 0) mesh_rec_kv(r, k, "-");
    else mesh_rec_kvf(r, k, "%.*f", p, v);
}

void mesh_rec_text(mesh_rec *r, const char *t) {
    free(r->text);
    r->text = mesh_xstrdup(t);
    for (char *c = r->text; *c; c++) if (*c == '\n' || *c == '\r') *c = ' ';
}

const char *mesh_rec_get(const mesh_rec *r, const char *k) {
    for (size_t i = 0; i < r->nkv; i++)
        if (strcmp(r->kv[i].k, k) == 0) return strcmp(r->kv[i].v, "-") == 0 ? NULL : r->kv[i].v;
    return NULL;
}

void mesh_views_text(mesh_buf *out, const mesh_recs *rs) {
    for (size_t i = 0; i < rs->len; i++) {
        const mesh_rec *r = &rs->items[i];
        mesh_buf_puts(out, r->kind);
        for (size_t j = 0; j < r->nkv; j++) mesh_buf_printf(out, " %s=%s", r->kv[j].k, r->kv[j].v);
        if (*r->text) mesh_buf_printf(out, " -- %s", r->text);
        mesh_buf_putc(out, '\n');
    }
}

static void json_str(mesh_buf *out, const char *s) {
    mesh_buf_putc(out, '"');
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        if (*c == '"') mesh_buf_puts(out, "\\\"");
        else if (*c == '\\') mesh_buf_puts(out, "\\\\");
        else if (*c < 0x20) mesh_buf_printf(out, "\\u%04x", *c);
        else mesh_buf_putc(out, (char)*c);
    }
    mesh_buf_putc(out, '"');
}

static bool digit(char c) { return c >= '0' && c <= '9'; }

bool mesh_views_json_number(const char *v) {
    size_t i = v[0] == '-', n = strlen(v);
    size_t s = i;
    while (i < n && digit(v[i])) i++;
    if (i == s) return false;
    if (i < n && v[i] == '.') {
        i++;
        s = i;
        while (i < n && digit(v[i])) i++;
        if (i == s) return false;
    }
    if (i < n && (v[i] == 'e' || v[i] == 'E')) {
        i++;
        if (i < n && (v[i] == '+' || v[i] == '-')) i++;
        s = i;
        while (i < n && digit(v[i])) i++;
        if (i == s) return false;
    }
    /* A leading zero followed by digits ("007") is not JSON: kept a string. */
    const char *in = v + (v[0] == '-');
    if (strlen(in) > 1 && in[0] == '0' && digit(in[1])) return false;
    return i == n;
}

static bool string_key(const char *k) {
    for (size_t i = 0; i < 37; i++) if (strcmp(MESH_VIEWS_STRING_KEYS[i], k) == 0) return true;
    return false;
}

void mesh_views_json(mesh_buf *out, const mesh_recs *rs) {
    mesh_buf_putc(out, '{');
    bool first_kind = true;
    for (size_t i = 0; i < rs->len; i++) {
        const char *kind = rs->items[i].kind;
        bool seen = false;
        for (size_t p = 0; p < i && !seen; p++) seen = strcmp(rs->items[p].kind, kind) == 0;
        if (seen) continue;
        if (!first_kind) mesh_buf_putc(out, ',');
        first_kind = false;
        json_str(out, kind);
        mesh_buf_puts(out, ":[");
        bool first = true;
        for (size_t j = i; j < rs->len; j++) {
            const mesh_rec *r = &rs->items[j];
            if (strcmp(r->kind, kind) != 0) continue;
            if (!first) mesh_buf_putc(out, ',');
            first = false;
            mesh_buf_putc(out, '{');
            for (size_t f = 0; f < r->nkv; f++) {
                if (f > 0) mesh_buf_putc(out, ',');
                json_str(out, r->kv[f].k);
                mesh_buf_putc(out, ':');
                const char *v = r->kv[f].v;
                if (strcmp(v, "-") == 0) mesh_buf_puts(out, "null");
                else if (!string_key(r->kv[f].k) && mesh_views_json_number(v)) mesh_buf_puts(out, v);
                else json_str(out, v);
            }
            if (*r->text) {
                if (r->nkv) mesh_buf_putc(out, ',');
                mesh_buf_puts(out, "\"text\":");
                json_str(out, r->text);
            }
            mesh_buf_putc(out, '}');
        }
        mesh_buf_putc(out, ']');
    }
    mesh_buf_puts(out, "}\n");
}

static bool blank(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) if (!(s[i] == ' ' || (s[i] >= 9 && s[i] <= 13))) return false;
    return true;
}

void mesh_views_parse(const char *text, mesh_recs *out) {
    const char *l = text;
    while (*l) {
        const char *e = strchr(l, '\n');
        size_t n = e ? (size_t)(e - l) : strlen(l);
        size_t ln = n;
        if (ln && l[ln - 1] == '\r') ln--; /* str::lines drops a \r before the \n */
        if (!blank(l, ln)) {
            char *line = mesh_xstrndup(l, ln);
            char *sep = strstr(line, " -- ");
            const char *txt = "";
            if (sep) { *sep = 0; txt = sep + 4; }
            mesh_rec *r = NULL;
            char *save = NULL;
            for (char *w = strtok_r(line, " \t\n\v\f\r", &save); w; w = strtok_r(NULL, " \t\n\v\f\r", &save)) {
                if (!r) { r = mesh_recs_add(out, w); continue; }
                char *eq = strchr(w, '=');
                if (eq) { *eq = 0; push_raw(r, w, mesh_xstrdup(eq + 1)); }
                else push_raw(r, w, mesh_xstrdup(""));
            }
            if (!r) r = mesh_recs_add(out, "");
            free(r->text);
            r->text = mesh_xstrdup(txt);
            free(line);
        }
        if (!e) break;
        l = e + 1;
    }
}

int mesh_views_format_of(const char *query, bool *json, char **err) {
    *json = false;
    const char *p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t n = amp ? (size_t)(amp - p) : strlen(p);
        if (n > 7 && strncmp(p, "format=", 7) == 0) {
            const char *v = p + 7;
            size_t vn = n - 7;
            if (vn == 4 && strncmp(v, "json", 4) == 0) { *json = true; return 0; }
            if (vn == 4 && strncmp(v, "text", 4) == 0) { *json = false; return 0; }
            *err = mesh_fmt("format=%.*s: json or text", (int)vn, v);
            return -1;
        }
        if (n == 7 && strncmp(p, "format=", 7) == 0) { *err = mesh_xstrdup("format=: json or text"); return -1; }
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

void mesh_views_render(mesh_buf *out, const mesh_recs *rs, bool json) {
    if (json) mesh_views_json(out, rs);
    else mesh_views_text(out, rs);
}
